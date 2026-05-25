/*
 * client_internal.h — internal types and helpers
 *
 * Maps the opaque C handles to the underlying C++ objects from ServerEngine.
 * Not part of the public API — do not include from client code.
 */

#ifndef CLIENT_INTERNAL_H
#define CLIENT_INTERNAL_H

#include "client.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

/* =========================================================================
 * Concrete handle types
 * ========================================================================= */

struct game_client_s {
    bool               initialized = false;
    bool               owns_loop   = false;
    std::string        last_error;
    mutable std::mutex error_mutex;
};

struct game_net_client_s {
    game_client_t*   engine;
    void*            ctx;          /* ClientCtx* from net_client_bind      */
    int              lua_ref;      /* Lua registry ref for instance table  */

    game_tcp_connect_cb_t  on_connect_cb  = nullptr;
    void*                  on_connect_ud  = nullptr;
    game_tcp_message_cb_t  on_message_cb  = nullptr;
    void*                  on_message_ud  = nullptr;
    game_tcp_close_cb_t    on_close_cb    = nullptr;
    void*                  on_close_ud    = nullptr;
};

struct game_net_server_s {
    game_client_t*   engine;
    void*            ctx;          /* ServerCtx* from net_server_bind      */

    game_tcp_server_connect_cb_t  on_connect_cb = nullptr;
    void*                         on_connect_ud = nullptr;
    game_tcp_server_message_cb_t  on_message_cb = nullptr;
    void*                         on_message_ud = nullptr;
    game_tcp_server_close_cb_t    on_close_cb   = nullptr;
    void*                         on_close_ud   = nullptr;
};

struct game_tcp_conn_s {
    game_net_server_t* server;
    void*              ctx;        /* ConnCtx* from net_server_bind        */
    int                lua_ref;    /* Lua registry ref for conn instance   */
    std::string        remote_addr;

    game_tcp_conn_message_cb_t  on_message_cb = nullptr;
    void*                       on_message_ud = nullptr;
    game_tcp_conn_close_cb_t    on_close_cb   = nullptr;
    void*                       on_close_ud   = nullptr;
};

struct game_udp_client_s {
    game_client_t* engine;
    void*          ctx;         /* UdpClientCtx* from net_udp_client_bind */
};

struct game_udp_server_s {
    game_client_t* engine;
    void*          ctx;         /* UdpServerCtx* from net_udp_server_bind */
};

struct game_kcp_client_s {
    game_client_t* engine;
    void*          ctx;         /* KcpClientCtx* from net_kcp_client_bind */
};

struct game_kcp_server_s {
    game_client_t* engine;
    void*          ctx;         /* KcpServerCtx* from net_kcp_server_bind */
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/** Set the last error string on an engine instance (thread-safe). */
inline void set_error(game_client_t* e, const char* msg) {
    if (!e || !msg) return;
    std::lock_guard<std::mutex> lock(e->error_mutex);
    e->last_error = msg;
}

inline void set_error_f(game_client_t* e, const char* fmt, ...) {
    if (!e || !fmt) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(e->error_mutex);
    e->last_error = buf;
}

#endif /* CLIENT_INTERNAL_H */
