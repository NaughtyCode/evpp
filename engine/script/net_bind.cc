#include "engine/script/net_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <evpp/tcp_client.h>
#include <evpp/tcp_server.h>
#include <evpp/tcp_conn.h>
#include <evpp/event_loop.h>
#include <evpp/buffer.h>
#include <evpp/httpc/request.h>
#include <evpp/httpc/response.h>

#include "engine/config/config.h"
#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/engine/engine.h"
#include "engine/vm/vm.h"

namespace engine {
namespace script {

namespace {

// ─── Lua callback helpers ──────────────────────────────────────────────

// Call a Lua function stored in the registry by reference.
// Logs and pops errors; does NOT unref.
void call_lua_callback(lua_State* L, int ref) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call a Lua function with one string argument.
void call_lua_callback_str(lua_State* L, int ref, const std::string& s) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushlstring(L, s.data(), s.size());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call a Lua function with (int, string) args — used by server on_connect/on_message/on_close.
void call_lua_callback_int_str(lua_State* L, int ref, int64_t n, const std::string& s) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, static_cast<lua_Integer>(n));
    lua_pushlstring(L, s.data(), s.size());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Call a Lua function with (int, string) for HTTP response.
void call_lua_http_handler(lua_State* L, int ref, int code, const std::string& body) {
    if (!L) return;
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, static_cast<lua_Integer>(code));
    lua_pushlstring(L, body.data(), body.size());
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[net.http] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ======================================================================
// TCP Client bindings
// ======================================================================

struct ClientCtx {
    std::unique_ptr<evpp::TCPClient> client;
    lua_State* L = nullptr;
    int on_connect_ref = LUA_NOREF;
    int on_message_ref = LUA_NOREF;
    int on_close_ref   = LUA_NOREF;
    bool is_connected = false;  // track so we can answer is_connected()
};

std::unordered_map<int64_t, std::shared_ptr<ClientCtx>> g_clients;
std::atomic<int64_t> g_next_client_id{1};

// ── l_net_client_connect(host_port, on_connect, on_message, on_close) → id ──
int l_net_client_connect(lua_State* L) {
    const char* addr = luaL_checkstring(L, 1);
    if (!*addr) {
        return luaL_error(L, "address must not be empty");
    }

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    auto ctx = std::make_shared<ClientCtx>();
    ctx->L = L;

    // Capture callback refs (args 2-4 are optional)
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

    int64_t cid = g_next_client_id.fetch_add(1);
    g_clients[cid] = ctx;

    auto name = std::string("lua_client_") + std::to_string(cid);
    ctx->client = std::make_unique<evpp::TCPClient>(loop, addr, name);
    ctx->client->set_auto_reconnect(false);

    auto weak_ctx = std::weak_ptr<ClientCtx>(ctx);

    ctx->client->SetConnectionCallback(
        [cid, L, weak_ctx](const evpp::TCPConnPtr& conn) {
            auto ctx = weak_ctx.lock();
            if (!ctx) return;

            if (conn->IsConnected()) {
                ctx->is_connected = true;
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger, "[net.client] connected: id=[{}] remote=[{}]",
                                cid, conn->remote_addr());
                call_lua_callback(L, ctx->on_connect_ref);
            } else {
                ctx->is_connected = false;
                auto* logger = GetLogger();
                ENGINE_LOG_INFO(logger, "[net.client] disconnected: id=[{}]", cid);
                call_lua_callback(L, ctx->on_close_ref);
                // Clean up callbacks on disconnect
                if (ctx->on_connect_ref != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
                }
                if (ctx->on_message_ref != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
                }
                if (ctx->on_close_ref != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
                }
                ctx->on_connect_ref = LUA_NOREF;
                ctx->on_message_ref = LUA_NOREF;
                ctx->on_close_ref = LUA_NOREF;
                // Defer erasing from g_clients to avoid destroying TCPClient
                // (owned by ClientCtx) while its callback stack is still active.
                auto* loop = Engine::Instance().GetEventLoop();
                if (loop) {
                    loop->RunInLoop([cid] { g_clients.erase(cid); });
                }
            }
        });

    ctx->client->SetMessageCallback(
        [L, weak_ctx](const evpp::TCPConnPtr& /*conn*/, evpp::Buffer* buf) {
            auto ctx = weak_ctx.lock();
            if (!ctx || ctx->on_message_ref == LUA_NOREF) return;
            std::string data = buf->NextAllString();
            call_lua_callback_str(L, ctx->on_message_ref, data);
        });

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.client] connecting to [{}], id=[{}]", addr, cid);
    ctx->client->Connect();

    lua_pushinteger(L, static_cast<lua_Integer>(cid));
    return 1;
}

// ── l_net_client_send(id, data) ──────────────────────────────────────────
int l_net_client_send(lua_State* L) {
    int64_t cid = luaL_checkinteger(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);

    auto it = g_clients.find(cid);
    if (it == g_clients.end()) {
        return luaL_error(L, "client not found: %lld", (long long)cid);
    }

    auto conn = it->second->client->conn();
    if (!conn || !conn->IsConnected()) {
        return luaL_error(L, "client not connected: %lld", (long long)cid);
    }

    conn->Send(data, len);
    return 0;
}

// ── l_net_client_disconnect(id) ──────────────────────────────────────────
int l_net_client_disconnect(lua_State* L) {
    int64_t cid = luaL_checkinteger(L, 1);

    auto it = g_clients.find(cid);
    if (it == g_clients.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.client] disconnecting id=[{}]", cid);
    it->second->client->Disconnect();

    lua_pushboolean(L, 1);
    return 1;
}

// ── l_net_client_is_connected(id) → bool ─────────────────────────────────
int l_net_client_is_connected(lua_State* L) {
    int64_t cid = luaL_checkinteger(L, 1);

    auto it = g_clients.find(cid);
    if (it == g_clients.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto conn = it->second->client->conn();
    bool ok = conn && conn->IsConnected();
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ── l_net_client_set_on_message(id, callback) ──────────────────────────────
int l_net_client_set_on_message(lua_State* L) {
    int64_t cid = luaL_checkinteger(L, 1);

    auto it = g_clients.find(cid);
    if (it == g_clients.end()) {
        return luaL_error(L, "client not found: %lld", (long long)cid);
    }

    auto& ctx = it->second;

    if (ctx->on_message_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        ctx->on_message_ref = LUA_NOREF;
    }

    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Re-set MessageCallback so TCPClient stores the current lambda
    auto weak_ctx = std::weak_ptr<ClientCtx>(ctx);
    ctx->client->SetMessageCallback(
        [L, weak_ctx](const evpp::TCPConnPtr&, evpp::Buffer* buf) {
            auto c = weak_ctx.lock();
            if (!c || c->on_message_ref == LUA_NOREF) return;
            std::string data = buf->NextAllString();
            call_lua_callback_str(L, c->on_message_ref, data);
        });

    return 0;
}

// ── l_net_client_set_on_close(id, callback) ────────────────────────────────
int l_net_client_set_on_close(lua_State* L) {
    int64_t cid = luaL_checkinteger(L, 1);

    auto it = g_clients.find(cid);
    if (it == g_clients.end()) {
        return luaL_error(L, "client not found: %lld", (long long)cid);
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
    // The ConnectionCallback lambda already reads ctx->on_close_ref via
    // weak_ptr at call time, so updating the ref is sufficient.

    return 0;
}

const luaL_Reg kClientFunctions[] = {
    {"connect",        l_net_client_connect},
    {"send",           l_net_client_send},
    {"disconnect",     l_net_client_disconnect},
    {"is_connected",   l_net_client_is_connected},
    {"set_on_message", l_net_client_set_on_message},
    {"set_on_close",   l_net_client_set_on_close},
    {nullptr, nullptr},
};

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

// Guards HTTP callbacks from firing after engine shutdown.
// Set to false in ShutdownNetBindings; HTTP callbacks check this before
// touching the Lua state (which may have been destroyed).
std::atomic<bool> g_net_alive{true};

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
                    call_lua_callback_int_str(L, close_it->second,
                        static_cast<int64_t>(conn_id), conn->remote_addr());
                    luaL_unref(L, LUA_REGISTRYINDEX, close_it->second);
                    ctx->conn_on_close_refs.erase(close_it);
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
    ctx->server->Stop();

    // Release server-wide Lua callbacks
    if (ctx->on_connect_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_connect_ref);
    if (ctx->on_message_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
    if (ctx->on_close_ref != LUA_NOREF)   luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_close_ref);
    // Release per-connection callback refs
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
    g_servers.erase(it);

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

// ======================================================================
// HTTP Client bindings
// ======================================================================

// Track HTTP callback refs so they can be released during shutdown even
// when the request hasn't completed yet. Without this, refs held by
// in-flight HTTP requests would leak in the Lua registry.
std::vector<int> g_http_pending_refs;

// ── l_net_http_get(url, on_response) ─────────────────────────────────────
int l_net_http_get(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_http_pending_refs.push_back(ref);

    double timeout = ConfigManager::Instance().GetServerConfig().http.timeout_sec;
    auto req = std::make_shared<evpp::httpc::GetRequest>(
        loop, url, evpp::Duration(timeout));

    req->Execute([L, ref](const std::shared_ptr<evpp::httpc::Response>& resp) {
        auto erase_ref = [&]() {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            auto it = std::find(g_http_pending_refs.begin(),
                                g_http_pending_refs.end(), ref);
            if (it != g_http_pending_refs.end()) g_http_pending_refs.erase(it);
        };
        // If engine is shutting down, ShutdownNetBindings already released
        // all pending refs — skip erase_ref to avoid double-unref.
        if (!g_net_alive.load()) {
            return;
        }
        if (ref == LUA_NOREF) {
            return;
        }
        if (resp) {
            std::string body(resp->body().data(), resp->body().size());
            call_lua_http_handler(L, ref, resp->http_code(), body);
        } else {
            call_lua_http_handler(L, ref, 0, "");
        }
        erase_ref();
    });

    return 0;
}

// ── l_net_http_post(url, body, on_response) ──────────────────────────────
int l_net_http_post(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    size_t body_len = 0;
    const char* body = luaL_checklstring(L, 2, &body_len);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    auto* loop = Engine::Instance().GetEventLoop();
    if (!loop) {
        return luaL_error(L, "EventLoop not available");
    }

    lua_pushvalue(L, 3);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_http_pending_refs.push_back(ref);

    double timeout = ConfigManager::Instance().GetServerConfig().http.timeout_sec;
    auto req = std::make_shared<evpp::httpc::PostRequest>(
        loop, url, std::string(body, body_len), evpp::Duration(timeout));

    req->Execute([L, ref](const std::shared_ptr<evpp::httpc::Response>& resp) {
        auto erase_ref = [&]() {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            auto it = std::find(g_http_pending_refs.begin(),
                                g_http_pending_refs.end(), ref);
            if (it != g_http_pending_refs.end()) g_http_pending_refs.erase(it);
        };
        if (!g_net_alive.load() || ref == LUA_NOREF) {
            erase_ref();
            return;
        }
        if (resp) {
            std::string body_str(resp->body().data(), resp->body().size());
            call_lua_http_handler(L, ref, resp->http_code(), body_str);
        } else {
            call_lua_http_handler(L, ref, 0, "");
        }
        erase_ref();
    });

    return 0;
}

const luaL_Reg kHttpFunctions[] = {
    {"get",  l_net_http_get},
    {"post", l_net_http_post},
    {nullptr, nullptr},
};

} // namespace

// ======================================================================
// Public API
// ======================================================================

void ExportNet(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    // Build nested "net" table:
    //   net = { client = { ... }, server = { ... }, http = { ... } }

    lua_createtable(L, 0, 3);           // net table

    // net.client
    luaL_newlib(L, kClientFunctions);         // net, client
    lua_setfield(L, -2, "client");

    // net.server
    luaL_newlib(L, kServerFunctions);         // net, server
    lua_setfield(L, -2, "server");

    // net.http
    luaL_newlib(L, kHttpFunctions);           // net, http
    lua_setfield(L, -2, "http");

    lua_setglobal(L, "net");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: net module exported "
                    "(net.client/net.server/net.http)");
}

void ShutdownNetBindings() {
    auto* logger = GetLogger();

    // Prevent any in-flight HTTP callbacks from touching a freed Lua state.
    g_net_alive.store(false);

    // Resolve a valid Lua state before clearing clients/servers, so we can
    // release pending HTTP refs even after the context maps are emptied.
    lua_State* L = nullptr;
    for (auto& [cid, ctx] : g_clients) {
        if (ctx->L) { L = ctx->L; break; }
    }
    if (!L) {
        for (auto& [sid, ctx] : g_servers) {
            if (ctx->L) { L = ctx->L; break; }
        }
    }
    if (!L) {
        L = Engine::Instance().GetScriptVM().GetState();
    }

    // Release pending HTTP callback refs before any Lua state is closed.
    if (!g_http_pending_refs.empty()) {
        if (L) {
            for (int ref : g_http_pending_refs) {
                if (ref != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                }
            }
        }
        size_t http_count = g_http_pending_refs.size();
        ENGINE_LOG_INFO(logger, "ScriptBind: released [{}] pending HTTP callback(s)", http_count);
        g_http_pending_refs.clear();
    }

    // Shutdown all clients — disconnect first, then release Lua refs
    for (auto& [cid, ctx] : g_clients) {
        (void)cid;
        ctx->client->Disconnect();
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
        }
    }
    size_t client_count = g_clients.size();
    g_clients.clear();

    // Shutdown all servers
    for (auto& [sid, ctx] : g_servers) {
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
    size_t server_count = g_servers.size();
    g_servers.clear();

    if (client_count > 0 || server_count > 0) {
        ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] client(s) and [{}] server(s)",
                        client_count, server_count);
    } else {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active net bindings to shut down");
    }
}

} // namespace script
} // namespace engine
