#include "runtime/script/net_server_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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
// TCP Server bindings
// ======================================================================

struct ServerCtx {
    std::unique_ptr<evpp::TCPServer> server;
    lua_State* L = nullptr;
    int on_connect_ref = LUA_NOREF;
    int on_message_ref = LUA_NOREF;
    int on_close_ref   = LUA_NOREF;
    // conn_id → TCPConnPtr for per-connection operations
    std::unordered_map<uint64_t, evpp::TCPConnPtr> conns;
    // Per-connection Lua callback refs (set via net.server.set_on_message / set_on_close)
    std::unordered_map<uint64_t, int> conn_on_message_refs;
    std::unordered_map<uint64_t, int> conn_on_close_refs;
};

std::unordered_map<int64_t, std::shared_ptr<ServerCtx>> g_servers;
std::atomic<int64_t> g_next_server_id{1};

// ─── Lua callback helpers ──────────────────────────────────────────────

// Call a Lua function with one string argument.
void call_lua_callback_str(lua_State* L, int ref, const std::string& s) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushlstring(L, s.data(), s.size());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.server] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call a Lua function with (int, string) args.
void call_lua_callback_int_str(lua_State* L, int ref, int64_t n, const std::string& s) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, static_cast<lua_Integer>(n));
    lua_pushlstring(L, s.data(), s.size());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.server] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ── l_net_server_listen(host_port, on_connect, on_message, on_close) → id ──
int l_net_server_listen(lua_State* L) {
    const char* addr = luaL_checkstring(L, 1);
    if (!*addr) {
        return luaL_error(L, "address must not be empty");
    }

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    auto ctx = std::make_shared<ServerCtx>();
    ctx->L = L;

    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_connect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (lua_gettop(L) >= 3 && lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (lua_gettop(L) >= 4 && lua_isfunction(L, 4)) {
        lua_pushvalue(L, 4);
        ctx->on_close_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    int64_t sid = g_next_server_id.fetch_add(1);
    g_servers[sid] = ctx;

    auto name = std::string("lua_server_") + std::to_string(sid);
    ctx->server = std::make_unique<evpp::TCPServer>(loop, addr, name, 0);
    // ^ thread_num=0: handle connections on the main EventLoop thread.
    //   Lua is not thread-safe, so we must not dispatch to worker threads.

    auto weak_ctx = std::weak_ptr<ServerCtx>(ctx);

    ctx->server->SetConnectionCallback(
        [sid, L, weak_ctx](const evpp::TCPConnPtr& conn) {
            auto ctx = weak_ctx.lock();
            if (!ctx) return;

            if (conn->IsConnected()) {
                uint64_t conn_id = conn->id();
                ctx->conns[conn_id] = conn;
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger,
                    "[net.server] new conn: server=[{}] conn=[{}] from=[{}]",
                    sid, conn_id, conn->remote_addr());
                call_lua_callback_int_str(L, ctx->on_connect_ref,
                    static_cast<int64_t>(conn_id), conn->remote_addr());
            } else {
                uint64_t conn_id = conn->id();
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger,
                    "[net.server] conn closed: server=[{}] conn=[{}]", sid, conn_id);

                // Per-connection close callback takes priority
                auto close_it = ctx->conn_on_close_refs.find(conn_id);
                if (close_it != ctx->conn_on_close_refs.end() && close_it->second != LUA_NOREF) {
                    int close_ref = close_it->second;  // extract before callback (may modify map)
                    call_lua_callback_int_str(L, close_ref,
                        static_cast<int64_t>(conn_id), conn->remote_addr());
                    luaL_unref(L, LUA_REGISTRYINDEX, close_ref);
                    ctx->conn_on_close_refs.erase(conn_id);
                } else {
                    call_lua_callback_int_str(L, ctx->on_close_ref,
                        static_cast<int64_t>(conn_id), conn->remote_addr());
                }

                // Clean up per-connection message ref
                auto msg_it = ctx->conn_on_message_refs.find(conn_id);
                if (msg_it != ctx->conn_on_message_refs.end()) {
                    if (msg_it->second != LUA_NOREF) {
                        luaL_unref(L, LUA_REGISTRYINDEX, msg_it->second);
                    }
                    ctx->conn_on_message_refs.erase(msg_it);
                }

                ctx->conns.erase(conn_id);
            }
        });

    ctx->server->SetMessageCallback(
        [L, weak_ctx](const evpp::TCPConnPtr& conn, evpp::Buffer* buf) {
            auto ctx = weak_ctx.lock();
            if (!ctx) return;
            uint64_t conn_id = conn->id();
            std::string data = buf->NextAllString();

            // Per-connection callback takes priority
            auto it = ctx->conn_on_message_refs.find(conn_id);
            if (it != ctx->conn_on_message_refs.end() && it->second != LUA_NOREF) {
                call_lua_callback_str(L, it->second, data);
                return;
            }

            // Fall back to server-wide callback
            if (ctx->on_message_ref == LUA_NOREF) return;
            call_lua_callback_int_str(L, ctx->on_message_ref,
                static_cast<int64_t>(conn_id), data);
        });

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.server] init & start, addr=[{}] server=[{}]", addr, sid);

    if (!ctx->server->Init()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
        g_servers.erase(sid);
        lua_pushnil(L);
        lua_pushstring(L, "server init failed");
        return 2;
    }

    if (!ctx->server->Start()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
        g_servers.erase(sid);
        lua_pushnil(L);
        lua_pushstring(L, "server start failed");
        return 2;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(sid));
    return 1;
}

// ── l_net_server_send(conn_id, data) ──────────────────────────────────────
int l_net_server_send(lua_State* L) {
    int64_t raw_conn_id = luaL_checkinteger(L, 1);
    auto conn_id = static_cast<uint64_t>(raw_conn_id);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);

    // Search all servers for this conn_id
    for (auto& [sid, ctx] : g_servers) {
        (void)sid;
        auto ci = ctx->conns.find(conn_id);
        if (ci != ctx->conns.end()) {
            if (ci->second->IsConnected()) {
                ci->second->Send(data, len);
                return 0;
            }
            return luaL_error(L, "connection closed: %lld", (long long)raw_conn_id);
        }
    }

    return luaL_error(L, "connection not found: %lld", (long long)raw_conn_id);
}

// ── l_net_server_close_conn(conn_id) ──────────────────────────────────────
int l_net_server_close_conn(lua_State* L) {
    int64_t raw_conn_id = luaL_checkinteger(L, 1);
    auto conn_id = static_cast<uint64_t>(raw_conn_id);

    for (auto& [sid, ctx] : g_servers) {
        (void)sid;
        auto ci = ctx->conns.find(conn_id);
        if (ci != ctx->conns.end()) {
            auto* logger = GetLogger();
            ENGINE_LOG_INFO(logger, "[net.server] closing conn=[{}]", raw_conn_id);
            ci->second->Close();
            // Use key-based erase — Close() may fire the disconnect callback
            // synchronously, which calls ctx->conns.erase(conn_id), invalidating ci.
            ctx->conns.erase(conn_id);

            // Clean up per-connection callback refs — the close callback
            // won't fire for a manual close, so we must release here.
            auto msg_it = ctx->conn_on_message_refs.find(conn_id);
            if (msg_it != ctx->conn_on_message_refs.end()) {
                if (msg_it->second != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, msg_it->second);
                }
                ctx->conn_on_message_refs.erase(msg_it);
            }
            auto close_it = ctx->conn_on_close_refs.find(conn_id);
            if (close_it != ctx->conn_on_close_refs.end()) {
                if (close_it->second != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, close_it->second);
                }
                ctx->conn_on_close_refs.erase(close_it);
            }

            lua_pushboolean(L, 1);
            return 1;
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}

// ── l_net_server_stop(server_id) ─────────────────────────────────────────
int l_net_server_stop(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_servers.find(sid);
    if (it == g_servers.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.server] stopping server=[{}]", sid);

    auto ctx = it->second;
    // Erase from g_servers BEFORE Stop() — Stop() fires Lua callbacks that
    // may call net.server.stop() re-entrantly. If we erased after Stop(),
    // the re-entrant erase invalidates `it` and causes UB at g_servers.erase(it).
    g_servers.erase(it);

    ctx->server->Stop();

    // Release server-wide Lua callbacks
    if (ctx->on_connect_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
    if (ctx->on_message_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
    if (ctx->on_close_ref != LUA_NOREF)   luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
    // Release per-connection callback refs (may have been partially cleaned
    // by disconnect callbacks fired during Stop() above).
    for (auto& [conn_id, ref] : ctx->conn_on_message_refs) {
        (void)conn_id;
        if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    ctx->conn_on_message_refs.clear();
    for (auto& [conn_id, ref] : ctx->conn_on_close_refs) {
        (void)conn_id;
        if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    ctx->conn_on_close_refs.clear();
    ctx->conns.clear();

    lua_pushboolean(L, 1);
    return 1;
}

// ── l_net_server_set_on_message(conn_id, callback) ──────────────────────────
int l_net_server_set_on_message(lua_State* L) {
    int64_t raw_conn_id = luaL_checkinteger(L, 1);
    auto conn_id = static_cast<uint64_t>(raw_conn_id);

    for (auto& [sid, ctx] : g_servers) {
        (void)sid;
        auto ci = ctx->conns.find(conn_id);
        if (ci == ctx->conns.end()) continue;

        // Unref old per-connection callback
        auto old_it = ctx->conn_on_message_refs.find(conn_id);
        if (old_it != ctx->conn_on_message_refs.end()) {
            if (old_it->second != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, old_it->second);
            }
            ctx->conn_on_message_refs.erase(old_it);
        }

        // Store new callback (or nil/absent → remove, falls back to server-wide)
        if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
            lua_pushvalue(L, 2);
            ctx->conn_on_message_refs[conn_id] = luaL_ref(L, LUA_REGISTRYINDEX);
        }

        return 0;
    }

    return luaL_error(L, "connection not found: %lld", (long long)raw_conn_id);
}

// ── l_net_server_set_on_close(conn_id, callback) ────────────────────────────
int l_net_server_set_on_close(lua_State* L) {
    int64_t raw_conn_id = luaL_checkinteger(L, 1);
    auto conn_id = static_cast<uint64_t>(raw_conn_id);

    for (auto& [sid, ctx] : g_servers) {
        (void)sid;
        auto ci = ctx->conns.find(conn_id);
        if (ci == ctx->conns.end()) continue;

        auto old_it = ctx->conn_on_close_refs.find(conn_id);
        if (old_it != ctx->conn_on_close_refs.end()) {
            if (old_it->second != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, old_it->second);
            }
            ctx->conn_on_close_refs.erase(old_it);
        }

        if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
            lua_pushvalue(L, 2);
            ctx->conn_on_close_refs[conn_id] = luaL_ref(L, LUA_REGISTRYINDEX);
        }

        return 0;
    }

    return luaL_error(L, "connection not found: %lld", (long long)raw_conn_id);
}

// ── l_net_server_set_on_connect(server_id, callback) ────────────────────────
int l_net_server_set_on_connect(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_servers.find(sid);
    if (it == g_servers.end()) {
        return luaL_error(L, "server not found: %lld", (long long)sid);
    }

    auto& ctx = it->second;

    if (ctx->on_connect_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
        ctx->on_connect_ref = LUA_NOREF;
    }

    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_connect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    // The ConnectionCallback lambda reads ctx->on_connect_ref via weak_ptr.

    return 0;
}

// ── l_net_server_set_on_disconnect(server_id, callback) ─────────────────────
// Updates the server-wide on_close fallback. Per-connection set_on_close
// takes priority for individual connections.
int l_net_server_set_on_disconnect(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_servers.find(sid);
    if (it == g_servers.end()) {
        return luaL_error(L, "server not found: %lld", (long long)sid);
    }

    auto& ctx = it->second;

    if (ctx->on_close_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
        ctx->on_close_ref = LUA_NOREF;
    }

    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_close_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    // The ConnectionCallback lambda reads ctx->on_close_ref via weak_ptr
    // and dispatches per-connection close ref first, then falls back to this.

    return 0;
}

const luaL_Reg kServerFunctions[] = {
    {"listen",            l_net_server_listen},
    {"send",              l_net_server_send},
    {"close_conn",        l_net_server_close_conn},
    {"stop",              l_net_server_stop},
    {"set_on_message",    l_net_server_set_on_message},
    {"set_on_close",      l_net_server_set_on_close},
    {"set_on_connect",    l_net_server_set_on_connect},
    {"set_on_disconnect", l_net_server_set_on_disconnect},
    {nullptr, nullptr},
};

} // namespace

// ======================================================================
// Public API
// ======================================================================

void PushServerLibrary(lua_State* L) {
    if (!L) return;
    luaL_newlib(L, kServerFunctions);
}

void ShutdownServerBindings() {
    auto* logger = GetLogger();

    // Move g_servers to a local before iterating — Stop() fires Lua
    // callbacks that may call net.server.stop(), which erases from
    // g_servers and would invalidate the range-for iterator.
    auto servers = std::move(g_servers);
    for (auto& [sid, ctx] : servers) {
        (void)sid;
        ctx->server->Stop();
        if (ctx->L) {
            if (ctx->on_connect_ref != LUA_NOREF) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
                ctx->on_connect_ref = LUA_NOREF;
            }
            if (ctx->on_message_ref != LUA_NOREF) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->on_message_ref);
                ctx->on_message_ref = LUA_NOREF;
            }
            if (ctx->on_close_ref != LUA_NOREF) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->on_close_ref);
                ctx->on_close_ref = LUA_NOREF;
            }
            // Clean up per-connection refs
            for (auto& [conn_id, ref] : ctx->conn_on_message_refs) {
                (void)conn_id;
                if (ref != LUA_NOREF) {
                    luaL_unref(ctx->L, LUA_REGISTRYINDEX, ref);
                }
            }
            ctx->conn_on_message_refs.clear();
            for (auto& [conn_id, ref] : ctx->conn_on_close_refs) {
                (void)conn_id;
                if (ref != LUA_NOREF) {
                    luaL_unref(ctx->L, LUA_REGISTRYINDEX, ref);
                }
            }
            ctx->conn_on_close_refs.clear();
        }
        ctx->conns.clear();
    }
    size_t server_count = servers.size();
    servers.clear();

    if (server_count > 0) {
        ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] server(s)", server_count);
    } else {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active net bindings to shut down");
    }
}

} // namespace script
} // namespace engine
