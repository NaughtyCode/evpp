/*
 * client_udp.cpp — UDP Client and UDP Server wrappers.
 *
 * UDP client operations are synchronous (blocking send/recv), so the
 * implementation calls the Lua bindings directly without callback bridging.
 * The UDP server uses an async callback dispatched on the event-loop thread;
 * a C callback receives each datagram.
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

/* ── Global registry for UDP server callbacks ─────────────────────────── */

std::mutex g_udp_srv_mutex;
std::unordered_map<void*, game_udp_message_cb_t> g_udp_srv_cb;
std::unordered_map<void*, void*>                 g_udp_srv_ud;

/* Lua trampoline for UDP server on_message(data, remote_ip) */
int udp_srv_trampoline(lua_State* L) {
    /* upvalue 1 = game_udp_server_t* */
    auto* srv = static_cast<game_udp_server_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!srv) return 0;

    size_t data_len = 0, ip_len = 0;
    const char* data = luaL_checklstring(L, 1, &data_len);
    const char* ip   = luaL_checklstring(L, 2, &ip_len);

    game_udp_message_cb_t cb = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_udp_srv_mutex);
        auto it_cb = g_udp_srv_cb.find(srv);
        auto it_ud = g_udp_srv_ud.find(srv);
        if (it_cb != g_udp_srv_cb.end()) cb = it_cb->second;
        if (it_ud != g_udp_srv_ud.end()) ud = it_ud->second;
    }

    if (cb) {
        cb(data, static_cast<int>(data_len), ip, ud);
    }
    return 0;
}

} /* anonymous namespace */

extern "C" {

/* =========================================================================
 * UDP Client
 * ========================================================================= */

game_error_t game_udp_connect(game_client_t* client,
                              const char* host, int port,
                              game_udp_client_t** out_udp) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!host || !*host || port <= 0 || port > 65535 || !out_udp)
        return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* udp = new (std::nothrow) game_udp_client_t();
    if (!udp) return GAME_ERR_OUT_OF_MEMORY;
    udp->engine = client;

    /* Call net.udp_client.connect(host, port) — returns instance or nil,err */
    lua_getglobal(L, "net");                   /* net                    */
    lua_getfield(L, -1, "udp_client");         /* net, udp_client        */
    lua_remove(L, -2);                         /* udp_client             */
    lua_getfield(L, -1, "connect");            /* udp_client, connect    */
    lua_remove(L, -2);                         /* connect                */
    lua_pushstring(L, host);                   /* connect, host          */
    lua_pushinteger(L, port);                  /* connect, host, port    */

    if (lua_pcall(L, 2, 2, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        delete udp;
        return GAME_ERR_NETWORK;
    }

    /* Check return: nil + errmsg means failure */
    if (lua_isnil(L, -2)) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 2);
        delete udp;
        return GAME_ERR_NETWORK;
    }

    /* Store the instance table ref */
    /* Top of stack: instance table (pos -2), err=nil (pos -1) */
    /* Actually the return is just the table on success (1 value) */
    /* Let's pop the err if present */
    if (lua_gettop(L) >= 2 && lua_isnil(L, -1)) {
        lua_pop(L, 1);  /* discard nil */
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    udp->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(ref));

    *out_udp = udp;
    return GAME_OK;
}

static int get_udp_ref(game_udp_client_t* udp) {
    return static_cast<int>(reinterpret_cast<intptr_t>(udp->ctx));
}

game_error_t game_udp_send(game_udp_client_t* udp,
                           const char* data, int data_len) {
    if (!udp || !data || data_len <= 0) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_ref(udp)); /* t       */
    lua_getfield(L, -1, "send");                          /* t, send */
    lua_insert(L, -2);                                    /* send, t */
    lua_pushlstring(L, data, data_len);                   /* send, t, d */

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return GAME_ERR_NOT_CONNECTED;
    }

    bool ok = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ok ? GAME_OK : GAME_ERR_NETWORK;
}

game_error_t game_udp_request(game_udp_client_t* udp,
                              const char* data, int data_len,
                              int timeout_ms,
                              char* resp_buf, int resp_cap,
                              int* out_resp_len) {
    if (!udp || !data || data_len <= 0 || !resp_buf || resp_cap <= 0)
        return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_ref(udp)); /* t            */
    lua_getfield(L, -1, "do_request");                    /* t, req       */
    lua_insert(L, -2);                                    /* req, t       */
    lua_pushlstring(L, data, data_len);                   /* req, t, d    */
    lua_pushinteger(L, timeout_ms);                       /* req, t, d, ms */

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

game_error_t game_udp_request_to(game_client_t* client,
                                 const char* host, int port,
                                 const char* data, int data_len,
                                 int timeout_ms,
                                 char* resp_buf, int resp_cap,
                                 int* out_resp_len) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!host || !*host || port <= 0 || port > 65535) return GAME_ERR_INVALID_ARG;
    if (!data || data_len <= 0 || !resp_buf || resp_cap <= 0) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    /* Call net.udp_client.do_request(host, port, data, timeout_ms) */
    lua_getglobal(L, "net");                   /* net                    */
    lua_getfield(L, -1, "udp_client");         /* net, udp_client        */
    lua_remove(L, -2);                         /* udp_client             */
    lua_getfield(L, -1, "do_request");         /* udp_client, do_request */
    lua_remove(L, -2);                         /* do_request             */
    lua_pushstring(L, host);                   /* dr, host               */
    lua_pushinteger(L, port);                  /* dr, host, port         */
    lua_pushlstring(L, data, data_len);        /* dr, host, port, data   */
    lua_pushinteger(L, timeout_ms);            /* dr, host, port, data, ms */

    if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
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

game_error_t game_udp_send_to(game_client_t* client,
                              const char* host, int port,
                              const char* data, int data_len) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!host || !*host || port <= 0 || port > 65535) return GAME_ERR_INVALID_ARG;
    if (!data || data_len <= 0) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    /* Call net.udp_client.send_to(host, port, data) */
    lua_getglobal(L, "net");                   /* net                    */
    lua_getfield(L, -1, "udp_client");         /* net, udp_client        */
    lua_remove(L, -2);                         /* udp_client             */
    lua_getfield(L, -1, "send_to");            /* udp_client, send_to    */
    lua_remove(L, -2);                         /* send_to                */
    lua_pushstring(L, host);                   /* st, host               */
    lua_pushinteger(L, port);                  /* st, host, port         */
    lua_pushlstring(L, data, data_len);        /* st, host, port, data   */

    if (lua_pcall(L, 3, 2, 0) != LUA_OK) {
        lua_pop(L, 1);
        return GAME_ERR_NETWORK;
    }

    bool ok = lua_toboolean(L, -2);  /* first return: bool */
    lua_pop(L, 2);
    return ok ? GAME_OK : GAME_ERR_NETWORK;
}

void game_udp_close(game_udp_client_t** udp_ptr) {
    if (!udp_ptr || !*udp_ptr) return;
    game_udp_client_t* udp = *udp_ptr;

    lua_State* L = get_L();
    if (L) {
        int ref = get_udp_ref(udp);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref); /* t            */
        lua_getfield(L, -1, "close");           /* t, close     */
        lua_insert(L, -2);                      /* close, t     */
        lua_pcall(L, 1, 0, 0);                 /* (empty)      */
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    delete udp;
    *udp_ptr = nullptr;
}

bool game_udp_is_connected(game_udp_client_t* udp) {
    if (!udp) return false;

    lua_State* L = get_L();
    if (!L) return false;

    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_ref(udp));
    lua_getfield(L, -1, "is_connected");
    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return false;
    }
    bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
}

/* =========================================================================
 * UDP Server
 * ========================================================================= */

game_error_t game_udp_listen(game_client_t* client, int port,
                             game_udp_message_cb_t cb, void* userdata,
                             game_udp_server_t** out_udp) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (port <= 0 || port > 65535 || !out_udp) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* srv = new (std::nothrow) game_udp_server_t();
    if (!srv) return GAME_ERR_OUT_OF_MEMORY;
    srv->engine = client;

    /* Register callback before calling listen */
    if (cb) {
        std::lock_guard<std::mutex> lock(g_udp_srv_mutex);
        g_udp_srv_cb[srv] = cb;
        g_udp_srv_ud[srv] = userdata;
    }

    /* Call net.udp_server.listen(port, callback) */
    lua_getglobal(L, "net");                   /* net                 */
    lua_getfield(L, -1, "udp_server");         /* net, udp_server     */
    lua_remove(L, -2);                         /* udp_server          */
    lua_getfield(L, -1, "listen");             /* udp_server, listen  */
    lua_remove(L, -2);                         /* listen              */
    lua_pushinteger(L, port);                  /* listen, port        */

    /* Push trampoline as callback with srv as upvalue */
    lua_pushlightuserdata(L, srv);             /* listen, port, srv   */
    lua_pushcclosure(L, udp_srv_trampoline, 1);/* listen, port, fn    */

    if (lua_pcall(L, 2, 2, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        delete srv;
        return GAME_ERR_NETWORK;
    }

    if (lua_isnil(L, -2)) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 2);
        {
            std::lock_guard<std::mutex> lock(g_udp_srv_mutex);
            g_udp_srv_cb.erase(srv);
            g_udp_srv_ud.erase(srv);
        }
        delete srv;
        return GAME_ERR_NETWORK;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    srv->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(ref));

    *out_udp = srv;
    return GAME_OK;
}

void game_udp_server_stop(game_udp_server_t** server_ptr) {
    if (!server_ptr || !*server_ptr) return;
    game_udp_server_t* srv = *server_ptr;

    {
        std::lock_guard<std::mutex> lock(g_udp_srv_mutex);
        g_udp_srv_cb.erase(srv);
        g_udp_srv_ud.erase(srv);
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

static int get_udp_srv_ref(game_udp_server_t* srv) {
    return static_cast<int>(reinterpret_cast<intptr_t>(srv->ctx));
}

void game_udp_server_pause(game_udp_server_t* srv) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_srv_ref(srv));
    lua_getfield(L, -1, "pause");
    lua_insert(L, -2);
    lua_pcall(L, 1, 0, 0);
}

void game_udp_server_resume(game_udp_server_t* srv) {
    if (!srv) return;
    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_srv_ref(srv));
    lua_getfield(L, -1, "continue");
    lua_insert(L, -2);
    lua_pcall(L, 1, 0, 0);
}

bool game_udp_server_is_running(game_udp_server_t* srv) {
    if (!srv) return false;
    lua_State* L = get_L();
    if (!L) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_srv_ref(srv));
    lua_getfield(L, -1, "is_running");
    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return false; }
    bool r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

void game_udp_server_set_on_message(game_udp_server_t* srv,
                                    game_udp_message_cb_t cb,
                                    void* userdata) {
    if (!srv) return;
    {
        std::lock_guard<std::mutex> lock(g_udp_srv_mutex);
        if (cb) {
            g_udp_srv_cb[srv] = cb;
            g_udp_srv_ud[srv] = userdata;
        } else {
            g_udp_srv_cb.erase(srv);
            g_udp_srv_ud.erase(srv);
        }
    }

    lua_State* L = get_L();
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, get_udp_srv_ref(srv));
    lua_getfield(L, -1, "set_on_message");
    lua_insert(L, -2);
    if (cb) {
        lua_pushlightuserdata(L, srv);
        lua_pushcclosure(L, udp_srv_trampoline, 1);
    } else {
        lua_pushnil(L);
    }
    lua_pcall(L, 2, 0, 0);
}

} /* extern "C" */
