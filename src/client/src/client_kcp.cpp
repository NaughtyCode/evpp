/*
 * client_kcp.cpp — KCP Client and KCP Server wrappers.
 *
 * KCP (reliable UDP) client and server operations are synchronous, so the
 * implementation calls the Lua bindings directly. The KCP server dispatches
 * received packets via a C callback on the event-loop thread.
 */

#include "client_internal.h"

#include "runtime/engine/engine.h"
#include "runtime/vm/vm.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace {

lua_State* get_L() {
    try {
        return engine::Engine::Instance().GetScriptVM().GetState();
    } catch (...) {
        return nullptr;
    }
}

/* ── KCP server callback registry ─────────────────────────────────────── */

std::mutex g_kcp_srv_mutex;
std::unordered_map<void*, game_kcp_message_cb_t> g_kcp_srv_cb;
std::unordered_map<void*, void*>                 g_kcp_srv_ud;

int kcp_srv_trampoline(lua_State* L) {
    auto* srv = static_cast<game_kcp_server_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!srv) return 0;

    size_t data_len = 0, ip_len = 0;
    const char* data = luaL_checklstring(L, 1, &data_len);
    const char* ip   = luaL_checklstring(L, 2, &ip_len);
    uint32_t conv    = static_cast<uint32_t>(luaL_checkinteger(L, 3));

    game_kcp_message_cb_t cb = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
        auto it_cb = g_kcp_srv_cb.find(srv);
        auto it_ud = g_kcp_srv_ud.find(srv);
        if (it_cb != g_kcp_srv_cb.end()) cb = it_cb->second;
        if (it_ud != g_kcp_srv_ud.end()) ud = it_ud->second;
    }

    if (cb) {
        cb(data, static_cast<int>(data_len), ip, conv, ud);
    }
    return 0;
}

} /* anonymous namespace */

extern "C" {

/* =========================================================================
 * KCP Client
 * ========================================================================= */

game_error_t game_kcp_connect(game_client_t* client,
                              const char* host, int port, uint32_t conv,
                              game_kcp_client_t** out_kcp) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!host || !*host || port <= 0 || port > 65535 || !out_kcp)
        return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* kcp = new (std::nothrow) game_kcp_client_t();
    if (!kcp) return GAME_ERR_OUT_OF_MEMORY;
    kcp->engine = client;

    /* Call net.kcp_client.connect(host, port, conv) */
    lua_getglobal(L, "net");                   /* net                    */
    lua_getfield(L, -1, "kcp_client");         /* net, kcp_client        */
    lua_remove(L, -2);                         /* kcp_client             */
    lua_getfield(L, -1, "connect");            /* kcp_client, connect    */
    lua_remove(L, -2);                         /* connect                */
    lua_pushstring(L, host);                   /* connect, host          */
    lua_pushinteger(L, port);                  /* connect, host, port    */
    lua_pushinteger(L, conv);                  /* connect, host, port, conv */

    if (lua_pcall(L, 3, 2, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        delete kcp;
        return GAME_ERR_NETWORK;
    }

    if (lua_isnil(L, -2)) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 2);
        delete kcp;
        return GAME_ERR_NETWORK;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    kcp->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(ref));

    *out_kcp = kcp;
    return GAME_OK;
}

game_error_t game_kcp_new(uint32_t conv, game_kcp_client_t** out_kcp) {
    if (!out_kcp) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* kcp = new (std::nothrow) game_kcp_client_t();
    if (!kcp) return GAME_ERR_OUT_OF_MEMORY;

    lua_getglobal(L, "net");                   /* net             */
    lua_getfield(L, -1, "kcp_client");         /* net, kcp_client */
    lua_remove(L, -2);                         /* kcp_client      */
    lua_getfield(L, -1, "new");                /* kcp_client, new */
    lua_remove(L, -2);                         /* new             */
    lua_pushinteger(L, conv);                  /* new, conv       */

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        delete kcp;
        return GAME_ERR_GENERIC;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    kcp->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(ref));

    *out_kcp = kcp;
    return GAME_OK;
}

static int get_kcp_ref(game_kcp_client_t* kcp) {
    return static_cast<int>(reinterpret_cast<intptr_t>(kcp->ctx));
}

game_error_t game_kcp_client_connect(game_kcp_client_t* kcp,
                                     const char* host, int port) {
    if (!kcp || !host || !*host || port <= 0 || port > 65535)
        return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "connect");
    lua_insert(L, -2);
    lua_pushstring(L, host);
    lua_pushinteger(L, port);

    if (lua_pcall(L, 3, 2, 0) != LUA_OK) {
        lua_pop(L, 1);
        return GAME_ERR_NETWORK;
    }

    bool ok = lua_toboolean(L, -2);
    if (!ok) {
        const char* err = lua_tostring(L, -1);
        if (err && kcp->engine) set_error(kcp->engine, err);
        lua_pop(L, 2);
        return GAME_ERR_NETWORK;
    }
    lua_pop(L, 1); /* pop the nil error */
    return GAME_OK;
}

game_error_t game_kcp_send(game_kcp_client_t* kcp,
                           const char* data, int data_len) {
    if (!kcp || !data || data_len <= 0) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "send");
    lua_insert(L, -2);
    lua_pushlstring(L, data, data_len);

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return GAME_ERR_NOT_CONNECTED;
    }

    bool ok = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ok ? GAME_OK : GAME_ERR_NETWORK;
}

game_error_t game_kcp_request(game_kcp_client_t* kcp,
                              const char* data, int data_len,
                              int timeout_ms,
                              char* resp_buf, int resp_cap,
                              int* out_resp_len) {
    if (!kcp || !data || data_len <= 0 || !resp_buf || resp_cap <= 0)
        return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "do_request");
    lua_insert(L, -2);
    lua_pushlstring(L, data, data_len);
    lua_pushinteger(L, timeout_ms);

    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        if (out_resp_len) *out_resp_len = 0;
        return GAME_ERR_TIMEOUT;
    }

    size_t len = 0;
    const char* resp = lua_tolstring(L, -1, &len);
    int copy_len = (static_cast<int>(len) < resp_cap)
                   ? static_cast<int>(len) : (resp_cap - 1);
    std::memcpy(resp_buf, resp, copy_len);
    resp_buf[copy_len] = '\0';
    if (out_resp_len) *out_resp_len = copy_len;
    lua_pop(L, 1);
    return GAME_OK;
}

void game_kcp_close(game_kcp_client_t** kcp_ptr) {
    if (!kcp_ptr || !*kcp_ptr) return;
    game_kcp_client_t* kcp = *kcp_ptr;

    lua_State* L = get_L();
    if (L) {
        int ref = get_kcp_ref(kcp);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_getfield(L, -1, "close");
        lua_insert(L, -2);
        lua_pcall(L, 1, 0, 0);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    delete kcp;
    *kcp_ptr = nullptr;
}

bool game_kcp_is_connected(game_kcp_client_t* kcp) {
    if (!kcp) return false;
    lua_State* L = get_L();
    if (!L) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "is_connected");
    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return false; }
    bool r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

void game_kcp_set_nodelay(game_kcp_client_t* kcp,
                          int nodelay, int interval, int resend, int nc) {
    if (!kcp) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "set_kcp_nodelay");
    lua_insert(L, -2);
    lua_pushinteger(L, nodelay);
    lua_pushinteger(L, interval);
    lua_pushinteger(L, resend);
    lua_pushinteger(L, nc);
    lua_pcall(L, 5, 0, 0);
}

void game_kcp_set_wnd_size(game_kcp_client_t* kcp, int sndwnd, int rcvwnd) {
    if (!kcp) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "set_kcp_wnd_size");
    lua_insert(L, -2);
    lua_pushinteger(L, sndwnd);
    lua_pushinteger(L, rcvwnd);
    lua_pcall(L, 3, 0, 0);
}

void game_kcp_set_mtu(game_kcp_client_t* kcp, int mtu) {
    if (!kcp) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "set_kcp_mtu");
    lua_insert(L, -2);
    lua_pushinteger(L, mtu);
    lua_pcall(L, 2, 0, 0);
}

void game_kcp_set_conv(game_kcp_client_t* kcp, uint32_t conv) {
    if (!kcp) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_ref(kcp));
    lua_getfield(L, -1, "set_kcp_conv");
    lua_insert(L, -2);
    lua_pushinteger(L, conv);
    lua_pcall(L, 2, 0, 0);
}

/* =========================================================================
 * KCP Server
 * ========================================================================= */

game_error_t game_kcp_listen(game_client_t* client, int port,
                             game_kcp_message_cb_t cb, void* userdata,
                             game_kcp_server_t** out_kcp) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (port <= 0 || port > 65535 || !out_kcp) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* srv = new (std::nothrow) game_kcp_server_t();
    if (!srv) return GAME_ERR_OUT_OF_MEMORY;
    srv->engine = client;

    if (cb) {
        std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
        g_kcp_srv_cb[srv] = cb;
        g_kcp_srv_ud[srv] = userdata;
    }

    lua_getglobal(L, "net");                   /* net                  */
    lua_getfield(L, -1, "kcp_server");         /* net, kcp_server      */
    lua_remove(L, -2);                         /* kcp_server           */
    lua_getfield(L, -1, "listen");             /* kcp_server, listen   */
    lua_remove(L, -2);                         /* listen               */
    lua_pushinteger(L, port);                  /* listen, port         */
    lua_pushlightuserdata(L, srv);
    lua_pushcclosure(L, kcp_srv_trampoline, 1); /* listen, port, fn    */

    if (lua_pcall(L, 2, 2, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        {
            std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
            g_kcp_srv_cb.erase(srv);
            g_kcp_srv_ud.erase(srv);
        }
        delete srv;
        return GAME_ERR_NETWORK;
    }

    if (lua_isnil(L, -2)) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 2);
        {
            std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
            g_kcp_srv_cb.erase(srv);
            g_kcp_srv_ud.erase(srv);
        }
        delete srv;
        return GAME_ERR_NETWORK;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    srv->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(ref));

    *out_kcp = srv;
    return GAME_OK;
}

void game_kcp_server_stop(game_kcp_server_t** server_ptr) {
    if (!server_ptr || !*server_ptr) return;
    game_kcp_server_t* srv = *server_ptr;

    {
        std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
        g_kcp_srv_cb.erase(srv);
        g_kcp_srv_ud.erase(srv);
    }

    lua_State* L = get_L();
    if (L) {
        int ref = static_cast<int>(reinterpret_cast<intptr_t>(srv->ctx));
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_getfield(L, -1, "stop");
        lua_insert(L, -2);
        lua_pcall(L, 1, 0, 0);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    delete srv;
    *server_ptr = nullptr;
}

static int get_kcp_srv_ref(game_kcp_server_t* srv) {
    return static_cast<int>(reinterpret_cast<intptr_t>(srv->ctx));
}

void game_kcp_server_pause(game_kcp_server_t* srv) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "pause");
    lua_insert(L, -2);
    lua_pcall(L, 1, 0, 0);
}

void game_kcp_server_resume(game_kcp_server_t* srv) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "continue");
    lua_insert(L, -2);
    lua_pcall(L, 1, 0, 0);
}

bool game_kcp_server_is_running(game_kcp_server_t* srv) {
    if (!srv) return false;
    lua_State* L = get_L();
    if (!L) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "is_running");
    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return false; }
    bool r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

void game_kcp_server_set_on_message(game_kcp_server_t* srv,
                                    game_kcp_message_cb_t cb,
                                    void* userdata) {
    if (!srv) return;
    {
        std::lock_guard<std::mutex> lock(g_kcp_srv_mutex);
        if (cb) {
            g_kcp_srv_cb[srv] = cb;
            g_kcp_srv_ud[srv] = userdata;
        } else {
            g_kcp_srv_cb.erase(srv);
            g_kcp_srv_ud.erase(srv);
        }
    }

    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "set_on_message");
    lua_insert(L, -2);
    if (cb) {
        lua_pushlightuserdata(L, srv);
        lua_pushcclosure(L, kcp_srv_trampoline, 1);
    } else {
        lua_pushnil(L);
    }
    lua_pcall(L, 2, 0, 0);
}

void game_kcp_server_set_nodelay(game_kcp_server_t* srv,
                                 int nodelay, int interval,
                                 int resend, int nc) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "set_kcp_nodelay");
    lua_insert(L, -2);
    lua_pushinteger(L, nodelay);
    lua_pushinteger(L, interval);
    lua_pushinteger(L, resend);
    lua_pushinteger(L, nc);
    lua_pcall(L, 5, 0, 0);
}

void game_kcp_server_set_wnd_size(game_kcp_server_t* srv,
                                  int sndwnd, int rcvwnd) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "set_kcp_wnd_size");
    lua_insert(L, -2);
    lua_pushinteger(L, sndwnd);
    lua_pushinteger(L, rcvwnd);
    lua_pcall(L, 3, 0, 0);
}

void game_kcp_server_set_mtu(game_kcp_server_t* srv, int mtu) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "set_kcp_mtu");
    lua_insert(L, -2);
    lua_pushinteger(L, mtu);
    lua_pcall(L, 2, 0, 0);
}

void game_kcp_server_set_session_timeout(game_kcp_server_t* srv,
                                         int timeout_ms) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_kcp_srv_ref(srv));
    lua_getfield(L, -1, "set_session_timeout");
    lua_insert(L, -2);
    lua_pushinteger(L, timeout_ms);
    lua_pcall(L, 2, 0, 0);
}

} /* extern "C" */
