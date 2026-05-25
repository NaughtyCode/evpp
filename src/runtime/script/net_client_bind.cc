#include "runtime/script/net_client_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <memory>
#include <string>

#include <runtime/evpp/tcp_client.h>
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
// TCP Client bindings (light userdata + Lua class)
// ======================================================================

struct ClientCtx {
    std::unique_ptr<evpp::TCPClient> client;
    lua_State* L = nullptr;
    int instance_ref = LUA_NOREF;  // ref to Lua class instance table
    bool is_connected = false;
    bool disposed = false;
};

const char* kClientMetaName = "net.client.instance";

// ── Internal helpers ─────────────────────────────────────────────────

// Get ClientCtx* from the light userdata stored at _ctx in the instance table.
ClientCtx* GetClientCtxFromTable(lua_State* L, int idx) {
    lua_getfield(L, idx, "_ctx");
    auto* ctx = static_cast<ClientCtx*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

// Call a no-arg method on the Lua instance by name.
void CallClientMethod(lua_State* L, int instance_ref, const char* method) {
    if (!L || instance_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, instance_ref);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_insert(L, -2);  // inst, func → func, inst (self)
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.client] {} error: {}", method,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call a one-string-arg method on the Lua instance by name.
void CallClientMethodStr(lua_State* L, int instance_ref, const char* method,
                          const std::string& arg) {
    if (!L || instance_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, instance_ref);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_insert(L, -2);                                 // func, inst
    lua_pushlstring(L, arg.data(), arg.size());        // func, inst, data
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.client] {} error: {}", method,
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ── l_net_client_connect(addr) → instance_table ──
int l_net_client_connect(lua_State* L) {
    const char* addr = luaL_checkstring(L, 1);
    if (!*addr) {
        return luaL_error(L, "address must not be empty");
    }

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    auto* ctx = new ClientCtx();
    ctx->L = L;

    // Build Lua class instance table
    lua_newtable(L);                                       // t

    // Store light userdata (ClientCtx*) as _ctx
    lua_pushlightuserdata(L, ctx);                         // t, lud
    lua_setfield(L, -2, "_ctx");                           // t

    // Apply metatable with methods and __gc
    luaL_getmetatable(L, kClientMetaName);                 // t, mt
    lua_setmetatable(L, -2);                               // t

    // Ref instance table in registry for callbacks
    lua_pushvalue(L, -1);                                  // t, t
    ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);    // t

    // Create TCPClient
    auto name = std::string("lua_client_") +
                std::to_string(reinterpret_cast<uintptr_t>(ctx));
    ctx->client = std::make_unique<evpp::TCPClient>(loop, addr, name);
    ctx->client->set_auto_reconnect(false);

    auto* L_ptr = L;
    int inst_ref = ctx->instance_ref;
    ClientCtx* ctx_ptr = ctx;

    ctx->client->SetConnectionCallback(
        [L_ptr, inst_ref, ctx_ptr](const evpp::TCPConnPtr& conn) {
            if (ctx_ptr->disposed) return;

            if (conn->IsConnected()) {
                ctx_ptr->is_connected = true;
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger, "[net.client] connected: remote=[{}]",
                                conn->remote_addr());
                CallClientMethod(L_ptr, inst_ref, "on_connect");
            } else {
                ctx_ptr->is_connected = false;
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger, "[net.client] disconnected");

                // Set disposed BEFORE dispatching on_close: the Lua handler
                // may call disconnect() re-entrantly which queues delete.
                // If we checked disposed after the callback, ctx_ptr could
                // already be freed.
                bool already_disposed = ctx_ptr->disposed;
                ctx_ptr->disposed = true;

                CallClientMethod(L_ptr, inst_ref, "on_close");

                if (!already_disposed) {
                    if (inst_ref != LUA_NOREF) {
                        lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, inst_ref);
                        lua_pushnil(L_ptr);
                        lua_setfield(L_ptr, -2, "_ctx");   // prevent use-after-free
                        lua_pop(L_ptr, 1);
                        luaL_unref(L_ptr, LUA_REGISTRYINDEX, inst_ref);
                        ctx_ptr->instance_ref = LUA_NOREF;
                    }
                    auto* loop = Engine::Instance().GetEventLoop();
                    if (loop) {
                        loop->RunInLoop([ctx_ptr] { delete ctx_ptr; });
                    } else {
                        delete ctx_ptr;
                    }
                }
            }
        });

    ctx->client->SetMessageCallback(
        [L_ptr, inst_ref, ctx_ptr](const evpp::TCPConnPtr&, evpp::Buffer* buf) {
            if (ctx_ptr->disposed) return;
            std::string data = buf->NextAllString();
            CallClientMethodStr(L_ptr, inst_ref, "on_message", data);
        });

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.client] connecting to [{}]", addr);
    ctx->client->Connect();

    return 1;  // return the instance table
}

// ── client:send(data) ────────────────────────────────────────────────────
int l_client_send(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "client: invalid context");
    if (ctx->disposed) return luaL_error(L, "client: closed");

    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);

    auto conn = ctx->client->conn();
    if (!conn || !conn->IsConnected()) {
        return luaL_error(L, "client: not connected");
    }

    conn->Send(data, len);
    return 0;
}

// ── client:disconnect() → bool ───────────────────────────────────────────
int l_client_disconnect(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    ctx->disposed = true;

    // Null out _ctx in the instance table to prevent use-after-free
    // from subsequent method calls after the deferred delete runs.
    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.client] disconnecting");

    // Clear callbacks before Disconnect() — TCPConn::Close() uses
    // QueueInLoop (always defers), so HandleClose may execute after
    // delete ctx below, and the stored callbacks capture raw ClientCtx*.
    ctx->client->SetConnectionCallback(evpp::ConnectionCallback());
    ctx->client->SetMessageCallback(evpp::MessageCallback());
    ctx->client->Disconnect();

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ClientCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    lua_pushboolean(L, 1);
    return 1;
}

// ── client:is_connected() → bool ─────────────────────────────────────────
int l_client_is_connected(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto conn = ctx->client->conn();
    lua_pushboolean(L, (conn && conn->IsConnected()) ? 1 : 0);
    return 1;
}

// ── __gc metamethod ──────────────────────────────────────────────────────
int l_client_gc(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx || ctx->disposed) return 0;

    ctx->disposed = true;
    ctx->is_connected = false;

    // Null out _ctx to prevent use-after-free from subsequent accesses
    // (though the table is being collected, methods may still be callable
    // from finalizer ordering edge cases).
    lua_pushnil(L);
    lua_setfield(L, 1, "_ctx");

    if (ctx->instance_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
        ctx->instance_ref = LUA_NOREF;
    }

    // Clear callbacks to prevent use-after-free (same rationale as
    // l_client_disconnect above).
    ctx->client->SetConnectionCallback(evpp::ConnectionCallback());
    ctx->client->SetMessageCallback(evpp::MessageCallback());
    ctx->client->Disconnect();

    auto* loop = Engine::Instance().GetEventLoop();
    if (loop) {
        ClientCtx* del_ctx = ctx;
        loop->RunInLoop([del_ctx] { delete del_ctx; });
    } else {
        delete ctx;
    }

    return 0;
}

// ── client:set_on_connect(callback) ────────────────────────────────────────
int l_client_set_on_connect(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "client: invalid context");
    if (ctx->disposed) return luaL_error(L, "client: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_connect");
    return 0;
}

// ── client:set_on_message(callback) ───────────────────────────────────────
int l_client_set_on_message(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "client: invalid context");
    if (ctx->disposed) return luaL_error(L, "client: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_message");
    return 0;
}

// ── client:set_on_close(callback) ─────────────────────────────────────────
int l_client_set_on_close(lua_State* L) {
    auto* ctx = GetClientCtxFromTable(L, 1);
    if (!ctx) return luaL_error(L, "client: invalid context");
    if (ctx->disposed) return luaL_error(L, "client: closed");
    lua_settop(L, 2);
    lua_setfield(L, 1, "on_close");
    return 0;
}

// ── Instance method table ─────────────────────────────────────────────────
const luaL_Reg kClientMethods[] = {
    {"send",            l_client_send},
    {"disconnect",      l_client_disconnect},
    {"is_connected",    l_client_is_connected},
    {"set_on_connect",  l_client_set_on_connect},
    {"set_on_message",  l_client_set_on_message},
    {"set_on_close",    l_client_set_on_close},
    {nullptr, nullptr},
};

// ── net.client static functions ───────────────────────────────────────────
const luaL_Reg kClientFunctions[] = {
    {"connect", l_net_client_connect},
    {nullptr, nullptr},
};

} // namespace

// ======================================================================
// Public API
// ======================================================================

void RegisterClientMetaTable(lua_State* L) {
    if (!L) return;

    // Register metatable for instance methods and __gc.
    // kClientMethods are set on the metatable itself; __index points back
    // to the metatable so instance:method() lookups hit it directly.
    luaL_newmetatable(L, kClientMetaName);         // mt
    lua_pushvalue(L, -1);                          // mt, mt
    lua_setfield(L, -2, "__index");                // mt.__index = mt
    luaL_setfuncs(L, kClientMethods, 0);           // mt
    lua_pushcfunction(L, l_client_gc);             // mt, gc
    lua_setfield(L, -2, "__gc");                   // mt
    lua_pop(L, 1);                                 // (empty)
}

void PushClientLibrary(lua_State* L) {
    if (!L) return;

    // net.client table (static functions only: connect)
    luaL_newlib(L, kClientFunctions);              // client
}

} // namespace script
} // namespace engine
