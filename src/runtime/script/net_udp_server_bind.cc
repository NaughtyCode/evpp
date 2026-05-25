#include "runtime/script/net_udp_server_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include <runtime/evpp/udp/udp_server.h>
#include <runtime/evpp/udp/udp_message.h>
#include <runtime/evpp/event_loop.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// ======================================================================
// UDP Server bindings
// ======================================================================

struct UdpServerCtx {
    std::unique_ptr<evpp::udp::Server> server;
    lua_State* L = nullptr;
    int on_message_ref = LUA_NOREF;
    int64_t server_id = 0;
};

std::unordered_map<int64_t, std::shared_ptr<UdpServerCtx>> g_udp_servers;
std::atomic<int64_t> g_next_udp_server_id{1};

// ── Bind the MessageHandler using weak_ptr (same pattern as TCP server) ──
void BindMessageHandler(const std::shared_ptr<UdpServerCtx>& ctx) {
    auto* main_loop = Engine::Instance().GetEventLoop();
    std::weak_ptr<UdpServerCtx> weak_ctx = ctx;

    ctx->server->SetMessageHandler(
        [weak_ctx, main_loop](evpp::EventLoop*, evpp::udp::MessagePtr& msg) {
            auto sp = weak_ctx.lock();
            if (!sp) return;

            int msg_ref = sp->on_message_ref;
            if (msg_ref == LUA_NOREF) return;
            if (!main_loop) return;

            // Snapshot message data — the Message buffer may be reused by the
            // recv thread on the next iteration.
            std::string data(msg->data(), msg->size());
            std::string remote_ip = msg->remote_ip();
            lua_State* L_ptr = sp->L;

            main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip]() {
                if (!L_ptr || msg_ref == LUA_NOREF) return;
                lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);
                if (lua_isnil(L_ptr, -1)) {    // ref was freed
                    lua_pop(L_ptr, 1);
                    return;
                }
                lua_pushlstring(L_ptr, data.data(), data.size());
                lua_pushlstring(L_ptr, remote_ip.data(), remote_ip.size());
                if (lua_pcall(L_ptr, 2, 0, 0) != LUA_OK) {
                    auto* logger = GetLogger();
                    ENGINE_LOG_ERROR(logger, "[net.udp_server] on_message error: {}",
                                     lua_tostring(L_ptr, -1));
                    lua_pop(L_ptr, 1);
                }
            });
        });
}

// ── l_udp_server_listen(port_or_ports, on_message) → server_id ─────────
int l_udp_server_listen(lua_State* L) {
    auto ctx = std::make_shared<UdpServerCtx>();
    ctx->L = L;

    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    int64_t sid = g_next_udp_server_id.fetch_add(1);
    ctx->server_id = sid;
    ctx->server = std::make_unique<evpp::udp::Server>();

    bool ok = false;
    // Number → single port; string → pass through (handles "5353" and "53,5353")
    if (lua_type(L, 1) == LUA_TNUMBER) {
        int port = static_cast<int>(luaL_checkinteger(L, 1));
        ok = ctx->server->Init(port);
    } else {
        const char* ports_str = luaL_checkstring(L, 1);
        ok = ctx->server->Init(ports_str);
    }

    if (!ok) {
        if (ctx->on_message_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        }
        lua_pushnil(L);
        lua_pushstring(L, "udp_server init failed");
        return 2;
    }

    BindMessageHandler(ctx);

    if (!ctx->server->Start()) {
        if (ctx->on_message_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        }
        lua_pushnil(L);
        lua_pushstring(L, "udp_server start failed");
        return 2;
    }

    g_udp_servers[sid] = ctx;

    auto* logger = GetLogger();
    std::string display = (lua_type(L, 1) == LUA_TNUMBER)
        ? std::to_string(lua_tointeger(L, 1))
        : std::string(lua_tostring(L, 1));
    ENGINE_LOG_INFO(logger, "[net.udp_server] listening on [{}], server=[{}]",
                    display, sid);

    lua_pushinteger(L, static_cast<lua_Integer>(sid));
    return 1;
}

// ── l_udp_server_stop(server_id) → bool ─────────────────────────────────
int l_udp_server_stop(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_udp_servers.find(sid);
    if (it == g_udp_servers.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[net.udp_server] stopping server=[{}]", sid);

    auto ctx = it->second;
    ctx->server->Stop(true);   // wait for recv threads to exit

    if (ctx->on_message_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        ctx->on_message_ref = LUA_NOREF;
    }

    g_udp_servers.erase(it);

    lua_pushboolean(L, 1);
    return 1;
}

// ── l_udp_server_pause(server_id) ──────────────────────────────────────
int l_udp_server_pause(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_udp_servers.find(sid);
    if (it == g_udp_servers.end()) {
        return luaL_error(L, "udp_server not found: %lld", (long long)sid);
    }

    it->second->server->Pause();
    return 0;
}

// ── l_udp_server_continue(server_id) ────────────────────────────────────
int l_udp_server_continue(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_udp_servers.find(sid);
    if (it == g_udp_servers.end()) {
        return luaL_error(L, "udp_server not found: %lld", (long long)sid);
    }

    it->second->server->Continue();
    return 0;
}

// ── l_udp_server_is_running(server_id) → bool ──────────────────────────
int l_udp_server_is_running(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_udp_servers.find(sid);
    if (it == g_udp_servers.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, it->second->server->IsRunning() ? 1 : 0);
    return 1;
}

// ── l_udp_server_set_on_message(server_id, callback) ────────────────────
int l_udp_server_set_on_message(lua_State* L) {
    int64_t sid = luaL_checkinteger(L, 1);

    auto it = g_udp_servers.find(sid);
    if (it == g_udp_servers.end()) {
        return luaL_error(L, "udp_server not found: %lld", (long long)sid);
    }

    auto& ctx = it->second;

    // Release old callback
    if (ctx->on_message_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
        ctx->on_message_ref = LUA_NOREF;
    }

    // Store new callback (or leave cleared if absent/nil)
    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Re-bind the MessageHandler so the new ref is captured
    BindMessageHandler(ctx);

    return 0;
}

const luaL_Reg kUdpServerFunctions[] = {
    {"listen",          l_udp_server_listen},
    {"stop",            l_udp_server_stop},
    {"pause",           l_udp_server_pause},
    {"continue",        l_udp_server_continue},
    {"is_running",      l_udp_server_is_running},
    {"set_on_message",  l_udp_server_set_on_message},
    {nullptr, nullptr},
};

} // namespace

// ======================================================================
// Public API
// ======================================================================

void PushUdpServerLibrary(lua_State* L) {
    if (!L) return;
    luaL_newlib(L, kUdpServerFunctions);
}

void ShutdownUdpServerBindings() {
    auto* logger = GetLogger();

    // Move to local before iterating — Stop(true) waits for threads;
    // a Lua callback queued before stop could theoretically call
    // net.udp_server.stop() which would mutate g_udp_servers.
    auto servers = std::move(g_udp_servers);
    for (auto& [sid, ctx] : servers) {
        (void)sid;
        ctx->server->Stop(true);
        if (ctx->L) {
            if (ctx->on_message_ref != LUA_NOREF) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->on_message_ref);
                ctx->on_message_ref = LUA_NOREF;
            }
        }
    }

    size_t server_count = servers.size();
    servers.clear();

    if (server_count > 0) {
        ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] UDP server(s)", server_count);
    } else {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active UDP server bindings to shut down");
    }
}

} // namespace script
} // namespace engine
