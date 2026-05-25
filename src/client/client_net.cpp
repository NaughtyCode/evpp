/*
 * client_net.cpp — TCP Client, TCP Server, and HTTP wrappers.
 *
 * Architecture: all network operations go through the existing Lua bindings
 * (net.client, net.server, net.http). This avoids duplicating C++ logic and
 * keeps the C API thin. C callbacks are bridged via Lua trampolines stored
 * on the instance tables.
 *
 * Each C handle (game_net_client_t, etc.) holds:
 *   - lua_ref : Lua registry reference to the instance table
 *   - C callback function pointers + userdata
 *
 * When a Lua callback fires, the trampoline retrieves the C handle from
 * the table's _capi field and dispatches to the stored C callback.
 */

#include "client_internal.h"

#include "runtime/engine/engine.h"
#include "runtime/vm/vm.h"

#include <cstring>
#include <unordered_map>
#include <mutex>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace {

/* ── Helper: get the ScriptVM lua_State ───────────────────────────────── */

lua_State* get_L() {
    try {
        return engine::Engine::Instance().GetScriptVM().GetState();
    } catch (...) {
        return nullptr;
    }
}

/* ── Helper: set a Lua method on the instance table ──────────────────── */

void set_lua_method(int ref, const char* name, lua_CFunction fn,
                    void* capi_handle) {
    lua_State* L = get_L();
    if (!L) return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);  /* t                */
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return; }

    /* Push the C API handle as upvalue 1, then create a closure. The
     * trampoline function receives the handle via upvalue. */
    lua_pushlightuserdata(L, capi_handle);   /* t, capi          */
    lua_pushcclosure(L, fn, 1);              /* t, fn            */
    lua_setfield(L, -2, name);               /* t                */
    lua_pop(L, 1);                           /* (empty)          */
}

/* ── Helper: call a no-arg method on a Lua instance table ────────────── */

bool call_method_0(int ref, const char* method, int nresults) {
    lua_State* L = get_L();
    if (!L) return false;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);  /* t                */
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
    lua_getfield(L, -1, method);             /* t, fn            */
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return false; }
    lua_insert(L, -2);                       /* fn, t (self)     */
    if (lua_pcall(L, 1, nresults, 0) != LUA_OK) {
        lua_pop(L, 1); /* error msg */
        return false;
    }
    return true;
}

/* ── Helper: call a 1-string-arg method ──────────────────────────────── */

bool call_method_s(int ref, const char* method, const char* data, int len) {
    lua_State* L = get_L();
    if (!L) return false;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);  /* t                */
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
    lua_getfield(L, -1, method);             /* t, fn            */
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return false; }
    lua_insert(L, -2);                       /* fn, t            */
    lua_pushlstring(L, data, len);           /* fn, t, s         */
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* ── Global registry: lua_ref → C handle (for trampoline dispatch) ──── */

std::mutex g_handle_mutex;
std::unordered_map<int, void*> g_ref_to_handle;

void register_handle(int ref, void* handle) {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    g_ref_to_handle[ref] = handle;
}

void unregister_handle(int ref) {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    g_ref_to_handle.erase(ref);
}

void* find_handle(int ref) {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    auto it = g_ref_to_handle.find(ref);
    return (it != g_ref_to_handle.end()) ? it->second : nullptr;
}

/* ── TCP Client trampolines (Lua → C callback dispatch) ────────────────── */

int tcp_on_connect_tramp(lua_State* L) {
    /* upvalue 1 = game_net_client_t* */
    auto* capi = static_cast<game_net_client_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (capi && capi->on_connect_cb) {
        capi->on_connect_cb(capi, capi->on_connect_ud);
    }
    return 0;
}

int tcp_on_message_tramp(lua_State* L) {
    auto* capi = static_cast<game_net_client_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (capi && capi->on_message_cb) {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 2, &len);
        capi->on_message_cb(capi, data, static_cast<int>(len),
                            capi->on_message_ud);
    }
    return 0;
}

int tcp_on_close_tramp(lua_State* L) {
    auto* capi = static_cast<game_net_client_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (capi && capi->on_close_cb) {
        capi->on_close_cb(capi, capi->on_close_ud);
    }
    return 0;
}

/* ── Helper: look up conn handle from Lua conn table ─────────────────── */

game_tcp_conn_t* conn_from_table(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return nullptr;
    lua_getfield(L, idx, "_capi");
    auto* conn = static_cast<game_tcp_conn_t*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return conn;
}

/* ── TCP Server + Conn trampolines ────────────────────────────────────── */

int tcp_srv_on_connect_tramp(lua_State* L) {
    auto* srv = static_cast<game_net_server_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    /* Lua args: self, conn_table, remote_addr */
    if (!srv || !srv->on_connect_cb) return 0;

    luaL_checktype(L, 2, LUA_TTABLE);
    size_t addr_len = 0;
    const char* addr = luaL_checklstring(L, 3, &addr_len);

    /* Create a C handle for this connection */
    auto* conn = new (std::nothrow) game_tcp_conn_t();
    if (!conn) return 0;
    conn->server = srv;
    conn->remote_addr.assign(addr, addr_len);

    /* Ref the Lua conn table */
    lua_pushvalue(L, 2);
    conn->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Store capi handle on the Lua table for later lookup */
    lua_pushvalue(L, 2);
    lua_pushlightuserdata(L, conn);
    lua_setfield(L, -2, "_capi");

    register_handle(conn->lua_ref, conn);

    srv->on_connect_cb(conn, conn->remote_addr.c_str(), srv->on_connect_ud);
    return 0;
}

/* Server-level on_message: Lua args are (self, conn_table, data) */
int tcp_srv_on_message_tramp(lua_State* L) {
    auto* srv = static_cast<game_net_server_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!srv || !srv->on_message_cb) return 0;

    auto* conn = conn_from_table(L, 2);
    if (!conn) return 0;

    size_t len = 0;
    const char* data = luaL_checklstring(L, 3, &len);
    srv->on_message_cb(conn, data, static_cast<int>(len), srv->on_message_ud);
    return 0;
}

/* Server-level on_close: Lua args are (self, conn_table, remote_addr) */
int tcp_srv_on_close_tramp(lua_State* L) {
    auto* srv = static_cast<game_net_server_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!srv || !srv->on_close_cb) return 0;

    auto* conn = conn_from_table(L, 2);
    if (!conn) return 0;

    size_t len = 0;
    const char* addr = luaL_checklstring(L, 3, &len);
    srv->on_close_cb(conn, addr, srv->on_close_ud);
    return 0;
}

int tcp_conn_on_message_tramp(lua_State* L) {
    auto* conn = static_cast<game_tcp_conn_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (conn && conn->on_message_cb) {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 2, &len);
        conn->on_message_cb(conn, data, static_cast<int>(len),
                            conn->on_message_ud);
    }
    return 0;
}

int tcp_conn_on_close_tramp(lua_State* L) {
    auto* conn = static_cast<game_tcp_conn_t*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (conn && conn->on_close_cb) {
        size_t len = 0;
        const char* addr = luaL_checklstring(L, 2, &len);
        conn->on_close_cb(conn, addr, conn->on_close_ud);
    }
    return 0;
}

/* ── HTTP trampoline ──────────────────────────────────────────────────── */

/* We store (cb, userdata) as upvalues so we don't need a global registry. */
int http_response_tramp(lua_State* L) {
    /* upvalue 1 = game_http_response_cb_t (as lightuserdata of fn ptr) */
    /* upvalue 2 = userdata (as lightuserdata) */
    auto* cb = reinterpret_cast<game_http_response_cb_t>(
        lua_touserdata(L, lua_upvalueindex(1)));
    void* ud = lua_touserdata(L, lua_upvalueindex(2));

    if (!cb) return 0;

    int http_code = static_cast<int>(luaL_checkinteger(L, 1));
    size_t body_len = 0;
    const char* body = nullptr;
    if (lua_gettop(L) >= 2) {
        body = luaL_checklstring(L, 2, &body_len);
    }
    cb(http_code, body, static_cast<int>(body_len), ud);
    return 0;
}

} /* anonymous namespace */

/* =========================================================================
 * TCP Client
 * ========================================================================= */

extern "C" {

game_error_t game_tcp_connect(game_client_t* client, const char* addr,
                              game_net_client_t** out_conn) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!addr || !*addr || !out_conn) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* conn = new (std::nothrow) game_net_client_t();
    if (!conn) return GAME_ERR_OUT_OF_MEMORY;
    conn->engine = client;

    /* Call net.client.connect(addr) */
    lua_getglobal(L, "net");                   /* net              */
    lua_getfield(L, -1, "client");             /* net, client      */
    lua_remove(L, -2);                         /* client           */
    lua_getfield(L, -1, "connect");            /* client, connect  */
    lua_remove(L, -2);                         /* connect          */
    lua_pushstring(L, addr);                   /* connect, addr    */

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        delete conn;
        return GAME_ERR_NETWORK;
    }

    /* Stack now has the instance table. Ref it. */
    conn->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Store capi handle on the Lua table so trampolines can find it */
    /* (Re-push the table since luaL_ref popped it) */
    lua_rawgeti(L, LUA_REGISTRYINDEX, conn->lua_ref);
    lua_pushlightuserdata(L, conn);
    lua_setfield(L, -2, "_capi");
    lua_pop(L, 1);

    register_handle(conn->lua_ref, conn);

    *out_conn = conn;
    return GAME_OK;
}

game_error_t game_tcp_send(game_net_client_t* conn,
                           const char* data, int data_len) {
    if (!conn || !data || data_len <= 0) return GAME_ERR_INVALID_ARG;

    if (!call_method_s(conn->lua_ref, "send", data, data_len)) {
        return GAME_ERR_NOT_CONNECTED;
    }
    return GAME_OK;
}

void game_tcp_disconnect(game_net_client_t** conn_ptr) {
    if (!conn_ptr || !*conn_ptr) return;

    game_net_client_t* conn = *conn_ptr;

    call_method_0(conn->lua_ref, "disconnect", 0);

    unregister_handle(conn->lua_ref);
    lua_State* L = get_L();
    if (L && conn->lua_ref != LUA_NOREF) {
        // Clear _capi to prevent dangling pointer in Lua-held references
        lua_rawgeti(L, LUA_REGISTRYINDEX, conn->lua_ref);
        lua_pushnil(L);
        lua_setfield(L, -2, "_capi");
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, conn->lua_ref);
    }
    delete conn;
    *conn_ptr = nullptr;
}

bool game_tcp_is_connected(game_net_client_t* conn) {
    if (!conn) return false;
    if (!call_method_0(conn->lua_ref, "is_connected", 1)) return false;
    lua_State* L = get_L();
    if (!L) return false;
    bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
}

void game_tcp_set_on_connect(game_net_client_t* conn,
                             game_tcp_connect_cb_t cb, void* userdata) {
    if (!conn) return;
    conn->on_connect_cb = cb;
    conn->on_connect_ud = userdata;
    if (cb) {
        set_lua_method(conn->lua_ref, "on_connect", tcp_on_connect_tramp, conn);
    }
}

void game_tcp_set_on_message(game_net_client_t* conn,
                             game_tcp_message_cb_t cb, void* userdata) {
    if (!conn) return;
    conn->on_message_cb = cb;
    conn->on_message_ud = userdata;
    if (cb) {
        set_lua_method(conn->lua_ref, "on_message", tcp_on_message_tramp, conn);
    }
}

void game_tcp_set_on_close(game_net_client_t* conn,
                           game_tcp_close_cb_t cb, void* userdata) {
    if (!conn) return;
    conn->on_close_cb = cb;
    conn->on_close_ud = userdata;
    if (cb) {
        set_lua_method(conn->lua_ref, "on_close", tcp_on_close_tramp, conn);
    }
}

/* =========================================================================
 * TCP Server
 * ========================================================================= */

game_error_t game_tcp_listen(game_client_t* client, const char* addr,
                             game_net_server_t** out_server) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!addr || !*addr || !out_server) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    auto* srv = new (std::nothrow) game_net_server_t();
    if (!srv) return GAME_ERR_OUT_OF_MEMORY;
    srv->engine = client;

    /* Call net.server.listen(addr) */
    lua_getglobal(L, "net");                   /* net              */
    lua_getfield(L, -1, "server");             /* net, server      */
    lua_remove(L, -2);                         /* server           */
    lua_getfield(L, -1, "listen");             /* server, listen   */
    lua_remove(L, -2);                         /* listen           */
    lua_pushstring(L, addr);                   /* listen, addr     */

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        delete srv;
        return GAME_ERR_NETWORK;
    }

    /* Store ref to server instance table */
    /* We store it as ctx (void*) for convenience — the actual ref
     * is needed to set callbacks, so store it in a local. */
    int srv_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    srv->ctx = reinterpret_cast<void*>(static_cast<intptr_t>(srv_ref));

    /* Store capi handle for trampolines */
    lua_rawgeti(L, LUA_REGISTRYINDEX, srv_ref);
    lua_pushlightuserdata(L, srv);
    lua_setfield(L, -2, "_capi");
    lua_pop(L, 1);

    *out_server = srv;
    return GAME_OK;
}

void game_tcp_server_stop(game_net_server_t** server_ptr) {
    if (!server_ptr || !*server_ptr) return;
    game_net_server_t* srv = *server_ptr;

    int srv_ref = static_cast<int>(reinterpret_cast<intptr_t>(srv->ctx));
    call_method_0(srv_ref, "stop", 0);

    lua_State* L = get_L();
    if (L && srv_ref != LUA_NOREF) {
        // Clear _capi to prevent dangling pointer in Lua-held references
        lua_rawgeti(L, LUA_REGISTRYINDEX, srv_ref);
        lua_pushnil(L);
        lua_setfield(L, -2, "_capi");
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, srv_ref);
    }
    delete srv;
    *server_ptr = nullptr;
}

static int get_srv_ref(game_net_server_t* srv) {
    return static_cast<int>(reinterpret_cast<intptr_t>(srv->ctx));
}

void game_tcp_server_set_on_connect(game_net_server_t* srv,
                                    game_tcp_server_connect_cb_t cb,
                                    void* userdata) {
    if (!srv) return;
    srv->on_connect_cb = cb;
    srv->on_connect_ud = userdata;
    if (cb) {
        set_lua_method(get_srv_ref(srv), "on_connect",
                       tcp_srv_on_connect_tramp, srv);
    }
}

void game_tcp_server_set_on_message(game_net_server_t* srv,
                                    game_tcp_server_message_cb_t cb,
                                    void* userdata) {
    if (!srv) return;
    srv->on_message_cb = cb;
    srv->on_message_ud = userdata;
    if (cb) {
        set_lua_method(get_srv_ref(srv), "on_message",
                       tcp_srv_on_message_tramp, srv);
    }
}

void game_tcp_server_set_on_close(game_net_server_t* srv,
                                  game_tcp_server_close_cb_t cb,
                                  void* userdata) {
    if (!srv) return;
    srv->on_close_cb = cb;
    srv->on_close_ud = userdata;
    if (cb) {
        set_lua_method(get_srv_ref(srv), "on_close",
                       tcp_srv_on_close_tramp, srv);
    }
}

/* =========================================================================
 * TCP Server — Per-connection operations
 * ========================================================================= */

game_error_t game_tcp_conn_send(game_tcp_conn_t* conn,
                                const char* data, int data_len) {
    if (!conn || !data || data_len <= 0) return GAME_ERR_INVALID_ARG;
    if (!call_method_s(conn->lua_ref, "send", data, data_len)) {
        return GAME_ERR_NOT_CONNECTED;
    }
    return GAME_OK;
}

void game_tcp_conn_close(game_tcp_conn_t** conn_ptr) {
    if (!conn_ptr || !*conn_ptr) return;
    game_tcp_conn_t* conn = *conn_ptr;

    call_method_0(conn->lua_ref, "close", 0);
    unregister_handle(conn->lua_ref);

    lua_State* L = get_L();
    if (L && conn->lua_ref != LUA_NOREF) {
        // Clear _capi to prevent dangling pointer in Lua-held references
        lua_rawgeti(L, LUA_REGISTRYINDEX, conn->lua_ref);
        lua_pushnil(L);
        lua_setfield(L, -2, "_capi");
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, conn->lua_ref);
    }
    delete conn;
    *conn_ptr = nullptr;
}

bool game_tcp_conn_is_connected(game_tcp_conn_t* conn) {
    if (!conn) return false;
    if (!call_method_0(conn->lua_ref, "is_connected", 1)) return false;
    lua_State* L = get_L();
    if (!L) return false;
    bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
}

void game_tcp_conn_set_on_message(game_net_server_t* /*server*/,
                                  game_tcp_conn_t* conn,
                                  game_tcp_conn_message_cb_t cb,
                                  void* userdata) {
    if (!conn) return;
    conn->on_message_cb = cb;
    conn->on_message_ud = userdata;
    if (cb) {
        set_lua_method(conn->lua_ref, "on_message",
                       tcp_conn_on_message_tramp, conn);
    }
}

void game_tcp_conn_set_on_close(game_net_server_t* /*server*/,
                                game_tcp_conn_t* conn,
                                game_tcp_conn_close_cb_t cb,
                                void* userdata) {
    if (!conn) return;
    conn->on_close_cb = cb;
    conn->on_close_ud = userdata;
    if (cb) {
        set_lua_method(conn->lua_ref, "on_close",
                       tcp_conn_on_close_tramp, conn);
    }
}

/* =========================================================================
 * HTTP Client
 * ========================================================================= */

game_error_t game_http_get(game_client_t* client, const char* url,
                           game_http_response_cb_t cb, void* userdata) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!url || !cb) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    /* Push http.get(url, closure(cb, userdata)) */
    lua_getglobal(L, "net");                   /* net              */
    lua_getfield(L, -1, "http");               /* net, http        */
    lua_remove(L, -2);                         /* http             */
    lua_getfield(L, -1, "get");                /* http, get        */
    lua_remove(L, -2);                         /* get              */
    lua_pushstring(L, url);                    /* get, url         */

    /* Create closure with cb and userdata as upvalues */
    lua_pushlightuserdata(L, reinterpret_cast<void*>(cb));   /* get, url, cb    */
    lua_pushlightuserdata(L, userdata);                      /* get, url, cb, ud */
    lua_pushcclosure(L, http_response_tramp, 2);             /* get, url, fn     */

    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        return GAME_ERR_GENERIC;
    }

    return GAME_OK;
}

game_error_t game_http_post(game_client_t* client, const char* url,
                            const char* body, int body_len,
                            game_http_response_cb_t cb, void* userdata) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!url || !cb) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_L();
    if (!L) return GAME_ERR_GENERIC;

    lua_getglobal(L, "net");                   /* net              */
    lua_getfield(L, -1, "http");               /* net, http        */
    lua_remove(L, -2);                         /* http             */
    lua_getfield(L, -1, "post");               /* http, post       */
    lua_remove(L, -2);                         /* post             */
    lua_pushstring(L, url);                    /* post, url        */
    if (body && body_len > 0) {
        lua_pushlstring(L, body, body_len);    /* post, url, body  */
    } else {
        lua_pushstring(L, "");                 /* post, url, ""    */
    }

    lua_pushlightuserdata(L, reinterpret_cast<void*>(cb));
    lua_pushlightuserdata(L, userdata);
    lua_pushcclosure(L, http_response_tramp, 2); /* post, url, body, fn */

    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        set_error(client, lua_tostring(L, -1));
        lua_pop(L, 1);
        return GAME_ERR_GENERIC;
    }

    return GAME_OK;
}

} /* extern "C" */
