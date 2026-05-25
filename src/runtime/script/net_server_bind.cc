#include "runtime/script/net_server_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_conn.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/buffer.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// ======================================================================
// TCP Server bindings (light userdata + Lua class instances)
// ======================================================================

struct ConnCtx {
    evpp::TCPConnPtr conn;
    lua_State* L = nullptr;
    int instance_ref = LUA_NOREF;        // Lua conn instance table
    int server_inst_ref = LUA_NOREF;     // Lua server instance (for fallback callbacks)
    bool disposed = false;
};

struct ServerCtx {
    std::unique_ptr<evpp::TCPServer> server;
    lua_State* L = nullptr;
    int instance_ref = LUA_NOREF;        // Lua server instance table
    bool disposed = false;
};

const char* kServerMetaName = "net.server.instance";
const char* kConnMetaName   = "net.server.conn.instance";

// Lightweight set of active ServerCtx pointers, used ONLY by
// ShutdownServerBindings to find and stop all servers during engine shutdown.
// Normal operations (send, close, stop, set_on_*) never touch this set.
std::unordered_set<ServerCtx*> g_server_ctxs;

// ── Internal helpers ─────────────────────────────────────────────────

ServerCtx* GetServerCtxFromTable(lua_State* L, int idx) {
    lua_getfield(L, idx, "_ctx");
    auto* ctx = static_cast<ServerCtx*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

ConnCtx* GetConnCtxFromTable(lua_State* L, int idx) {
    lua_getfield(L, idx, "_ctx");
    auto* ctx = static_cast<ConnCtx*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

// ── Callback dispatchers ─────────────────────────────────────────────

// Call inst:method(str) — for conn.on_message(self, data), conn.on_close(self, addr)
void CallInstMethodStr(lua_State* L, int inst_ref, const char* method,
                       const std::string& arg) {
    if (!L || inst_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_insert(L, -2);                                  // func, inst
    lua_pushlstring(L, arg.data(), arg.size());         // func, inst, str
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.server] {} error: {}", method,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call inst:method(table, str) — for server.on_connect(self, conn, addr),
// server.on_message(self, conn, data), server.on_close(self, conn, addr)
void CallInstMethodTableStr(lua_State* L, int inst_ref, const char* method,
                            int table_ref, const std::string& arg) {
    if (!L || inst_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_insert(L, -2);                                   // func, inst
    lua_rawgeti(L, LUA_REGISTRYINDEX, table_ref);        // func, inst, table
    lua_pushlstring(L, arg.data(), arg.size());          // func, inst, table, str
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.server] {} error: {}", method,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Check whether a method on an instance table is a function.
bool HasMethod(lua_State* L, int inst_ref, const char* method) {
    if (!L || inst_ref == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
    bool ok = (lua_getfield(L, -1, method) == LUA_TFUNCTION);
    lua_pop(L, 2);
    return ok;
}

// ── Connection methods ───────────────────────────────────────────────

int l_conn_send(lua_State* L) {
    auto* ctx = GetConnCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "conn: invalid context");
    if (ctx->disposed) return luaL_error(L, "conn: closed");

    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);

    if (!ctx->conn->IsConnected()) {
        return luaL_error(L, "conn: not connected");
    }

    ctx->conn->Send(data, len);
    return 0;
}

int l_conn_close(lua_State* L) {
    auto* ctx = GetConnCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    ctx->disposed = true;

    // Null _ctx to prevent use-after-free from subsequent method calls.
    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    // Clear the TCPConn context so future callbacks see nullptr.
    ctx->conn->set_context(evpp::Any());

    ctx->conn->Close();
    // Close() may fire the disconnect callback synchronously, but it
    // checks ctx->disposed and returns early — on_close is NOT called
    // for a manual close.

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ConnCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    lua_pushboolean(L, 1);
    return 1;
}

int l_conn_set_on_message(lua_State* L) {
    auto* ctx = GetConnCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "conn: invalid context");
    if (ctx->disposed) return luaL_error(L, "conn: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_message");
    return 0;
}

int l_conn_set_on_close(lua_State* L) {
    auto* ctx = GetConnCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "conn: invalid context");
    if (ctx->disposed) return luaL_error(L, "conn: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_close");
    return 0;
}

int l_conn_gc(lua_State* L) {
    auto* ctx = GetConnCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) return 0;

    ctx->disposed = true;

    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    ctx->conn->set_context(evpp::Any());
    ctx->conn->Close();

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ConnCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    return 0;
}

// ── Server methods ───────────────────────────────────────────────────

int l_server_stop(lua_State* L) {
    auto* ctx = GetServerCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    ctx->disposed = true;

    // Remove from shutdown tracking BEFORE Stop() — Stop() fires Lua
    // callbacks that may call server:stop() re-entrantly; the inner
    // stop would otherwise try to erase from g_server_ctxs a second time.
    g_server_ctxs.erase(ctx);

    ctx->server->Stop();

    // Null _ctx to prevent use-after-free from subsequent method calls.
    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.server] server stopped");

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ServerCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    lua_pushboolean(L, 1);
    return 1;
}

int l_server_set_on_connect(lua_State* L) {
    auto* ctx = GetServerCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "server: invalid context");
    if (ctx->disposed) return luaL_error(L, "server: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_connect");
    return 0;
}

int l_server_set_on_close(lua_State* L) {
    auto* ctx = GetServerCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "server: invalid context");
    if (ctx->disposed) return luaL_error(L, "server: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_close");
    return 0;
}

int l_server_gc(lua_State* L) {
    auto* ctx = GetServerCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) return 0;

    ctx->disposed = true;
    g_server_ctxs.erase(ctx);

    ctx->server->Stop();

    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ServerCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    return 0;
}

// ── net.server.listen(addr) → server_instance ────────────────────────

int l_net_server_listen(lua_State* L) {
    const char* addr = luaL_checkstring(L, 1);
    if (!*addr) {
        return luaL_error(L, "address must not be empty");
    }

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    auto* ctx = new ServerCtx();
    ctx->L = L;

    // Build Lua class instance table
    lua_newtable(L);                                       // t

    lua_pushlightuserdata(L, ctx);                         // t, lud
    lua_setfield(L, -2, "_ctx");                           // t

    luaL_getmetatable(L, kServerMetaName);                 // t, mt
    lua_setmetatable(L, -2);                               // t

    lua_pushvalue(L, -1);                                  // t, t
    ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);    // t

    // Create TCPServer
    auto name = std::string("lua_server_") +
                std::to_string(reinterpret_cast<uintptr_t>(ctx));
    ctx->server = std::make_unique<evpp::TCPServer>(loop, addr, name, 0);
    // thread_num=0: handle connections on the main EventLoop thread.

    auto* L_ptr = L;
    int server_inst_ref = ctx->instance_ref;
    ServerCtx* ctx_ptr = ctx;

    // ── Connection callback (connect / disconnect) ─────────────────
    ctx->server->SetConnectionCallback(
        [L_ptr, server_inst_ref, ctx_ptr](const evpp::TCPConnPtr& conn) {
            if (ctx_ptr->disposed) return;

            if (conn->IsConnected()) {
                // ── New connection ─────────────────────────────
                auto* conn_ctx = new ConnCtx();
                conn_ctx->L = L_ptr;
                conn_ctx->conn = conn;
                conn_ctx->server_inst_ref = server_inst_ref;

                // Build Lua conn instance table
                lua_newtable(L_ptr);                                    // ct

                lua_pushlightuserdata(L_ptr, conn_ctx);                 // ct, lud
                lua_setfield(L_ptr, -2, "_ctx");                        // ct

                luaL_getmetatable(L_ptr, kConnMetaName);                // ct, mt
                lua_setmetatable(L_ptr, -2);                            // ct

                lua_pushvalue(L_ptr, -1);                               // ct, ct
                conn_ctx->instance_ref = luaL_ref(L_ptr,
                    LUA_REGISTRYINDEX);                                 // ct

                // Store ConnCtx* in TCPConn context for O(1) dispatch.
                conn->set_context(evpp::Any(conn_ctx));

                int conn_inst_ref = conn_ctx->instance_ref;
                auto remote = conn->remote_addr();

                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger,
                    "[net.server] new conn: conn=[{}] from=[{}]",
                    conn->id(), remote);

                CallInstMethodTableStr(L_ptr, server_inst_ref,
                    "on_connect", conn_inst_ref, remote);

                lua_pop(L_ptr, 1);  // pop conn table (anchored in registry)

            } else {
                // ── Disconnect ─────────────────────────────────
                auto* conn_ctx = conn->context().Get<ConnCtx*>();
                if (!conn_ctx || conn_ctx->disposed) return;

                uint64_t raw_id = conn->id();
                auto remote = conn->remote_addr();

                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger,
                    "[net.server] conn closed: conn=[{}]", raw_id);

                int conn_ref = conn_ctx->instance_ref;
                int sv_ref   = conn_ctx->server_inst_ref;

                // Per-connection on_close overrides server-wide.
                if (HasMethod(L_ptr, conn_ref, "on_close")) {
                    CallInstMethodStr(L_ptr, conn_ref, "on_close", remote);
                } else {
                    CallInstMethodTableStr(L_ptr, sv_ref, "on_close",
                                           conn_ref, remote);
                }

                // If on_close called conn:close() re-entrantly, ConnCtx was
                // already cleaned up — avoid double-unref / double-delete.
                if (conn_ctx->disposed) return;

                // Clean up ConnCtx
                conn_ctx->disposed = true;
                lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, conn_ref);
                lua_pushnil(L_ptr);
                lua_setfield(L_ptr, -2, "_ctx");
                lua_pop(L_ptr, 1);

                if (conn_ref != LUA_NOREF) {
                    luaL_unref(L_ptr, LUA_REGISTRYINDEX, conn_ref);
                }
                conn->set_context(evpp::Any());

                auto* loop = Engine::Instance().GetEventLoop();
                if (loop) {
                    ConnCtx* del_ctx = conn_ctx;
                    loop->RunInLoop([del_ctx] { delete del_ctx; });
                }
            }
        });

    // ── Message callback ──────────────────────────────────────────
    ctx->server->SetMessageCallback(
        [L_ptr](const evpp::TCPConnPtr& conn, evpp::Buffer* buf) {
            auto* conn_ctx = conn->context().Get<ConnCtx*>();
            if (!conn_ctx || conn_ctx->disposed) return;

            int conn_ref = conn_ctx->instance_ref;
            int sv_ref   = conn_ctx->server_inst_ref;
            std::string data = buf->NextAllString();

            // Per-connection on_message overrides server-wide.
            if (HasMethod(L_ptr, conn_ref, "on_message")) {
                CallInstMethodStr(L_ptr, conn_ref, "on_message", data);
            } else {
                CallInstMethodTableStr(L_ptr, sv_ref, "on_message",
                                       conn_ref, data);
            }
        });

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.server] init & start, addr=[{}]", addr);

    if (!ctx->server->Init()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        lua_pushnil(L);
        lua_setfield(L, -2, "_ctx");  // null _ctx before delete (table at -2 after push)
        delete ctx;
        return luaL_error(L, "server init failed");
    }

    if (!ctx->server->Start()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        lua_pushnil(L);
        lua_setfield(L, -2, "_ctx");  // null _ctx before delete (table at -2 after push)
        delete ctx;
        return luaL_error(L, "server start failed");
    }

    // Track for shutdown
    g_server_ctxs.insert(ctx);

    return 1;  // return the server instance table
}

// ── Metatable registrations ──────────────────────────────────────────

const luaL_Reg kConnMethods[] = {
    {"send",            l_conn_send},
    {"close",           l_conn_close},
    {"set_on_message",  l_conn_set_on_message},
    {"set_on_close",    l_conn_set_on_close},
    {nullptr, nullptr},
};

const luaL_Reg kServerMethods[] = {
    {"stop",             l_server_stop},
    {"set_on_connect",   l_server_set_on_connect},
    {"set_on_close",     l_server_set_on_close},
    {nullptr, nullptr},
};

const luaL_Reg kServerFunctions[] = {
    {"listen", l_net_server_listen},
    {nullptr, nullptr},
};

} // namespace

// ======================================================================
// Metatable registration
// ======================================================================

void RegisterConnMetaTable(lua_State* L) {
    if (!L) return;
    luaL_newmetatable(L, kConnMetaName);           // mt
    lua_pushvalue(L, -1);                          // mt, mt
    lua_setfield(L, -2, "__index");                // mt.__index = mt
    luaL_setfuncs(L, kConnMethods, 0);             // mt
    lua_pushcfunction(L, l_conn_gc);               // mt, gc
    lua_setfield(L, -2, "__gc");                   // mt
    lua_pop(L, 1);
}

void RegisterServerMetaTable(lua_State* L) {
    if (!L) return;
    luaL_newmetatable(L, kServerMetaName);         // mt
    lua_pushvalue(L, -1);                          // mt, mt
    lua_setfield(L, -2, "__index");                // mt.__index = mt
    luaL_setfuncs(L, kServerMethods, 0);           // mt
    lua_pushcfunction(L, l_server_gc);             // mt, gc
    lua_setfield(L, -2, "__gc");                   // mt
    lua_pop(L, 1);
}

// ======================================================================
// Public API
// ======================================================================

void PushServerLibrary(lua_State* L) {
    if (!L) return;
    luaL_newlib(L, kServerFunctions);
}

void ShutdownServerBindings() {
    auto* logger = GetLogger();

    // Move to local before iterating — Stop() fires Lua callbacks that
    // may call server:stop() re-entrantly, which erases from g_server_ctxs.
    auto ctxs = std::move(g_server_ctxs);
    for (auto* ctx : ctxs) {
        if (ctx->disposed) continue;
        ctx->disposed = true;
        ctx->server->Stop();
        if (ctx->L && ctx->instance_ref != LUA_NOREF) {
            luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->instance_ref);
            ctx->instance_ref = LUA_NOREF;
        }
        delete ctx;
    }

    if (!ctxs.empty()) {
        ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] server(s)", ctxs.size());
    } else {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active net bindings to shut down");
    }
}

} // namespace script
} // namespace engine
