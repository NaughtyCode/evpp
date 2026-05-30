/*
 * client.h — Pure C API for the evpp Runtime
 *
 * A stable, ABI-safe C interface to the embedded runtime. Designed for
 * integration into game engines (Unity, Unreal Engine, custom C/C++ engines)
 * so that game client and server can share the same Lua business logic.
 *
 * All types are opaque handles. All strings are UTF-8. All functions use
 * extern "C" linkage and are safe to call via [DllImport] (C#) or dlsym.
 *
 * ## Quick start
 *
 *     game_client_t* cli;
 *     game_client_create(&cli);
 *     game_client_init(cli, "./config");
 *     game_client_do_file(cli, "main.lua", err, sizeof(err));
 *     // In your game loop:
 *     while (running) {
 *         game_client_tick(cli);
 *     }
 *     game_client_destroy(cli);
 *
 * ## Threading model
 *
 * All functions must be called from the same thread that drives the event
 * loop (the "main thread"). Network callbacks fire on this thread. If you
 * integrate into a game engine, call game_client_tick() on your main/game
 * thread and dispatch callbacks from there.
 *
 * ## Memory ownership
 *
 * - Opaque handles returned by create/connect/listen functions are owned by
 *   the caller and must be released with the matching _destroy/_close/_stop
 *   function.
 * - String data passed to callbacks is valid only for the duration of the
 *   callback. Copy it if you need it beyond the callback scope.
 * - userdata pointers are opaque to the library and never dereferenced.
 */

#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Platform detection & DLL export
 * ========================================================================= */

#if defined(_WIN32) || defined(_WIN64)
  #ifdef CLIENT_BUILD
    #define CLIENT_API __declspec(dllexport)
  #else
    #define CLIENT_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define CLIENT_API __attribute__((visibility("default")))
#else
  #define CLIENT_API
#endif

/* =========================================================================
 * Version
 * ========================================================================= */

#define CLIENT_VERSION_MAJOR 1
#define CLIENT_VERSION_MINOR 0
#define CLIENT_VERSION_PATCH 0
#define CLIENT_VERSION_STRING "1.0.0"

CLIENT_API const char* game_version(void);

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    GAME_OK                   =  0,  /* Success */
    GAME_ERR_GENERIC          = -1,  /* Unspecified error */
    GAME_ERR_INVALID_ARG      = -2,  /* NULL or out-of-range argument */
    GAME_ERR_NOT_FOUND        = -3,  /* Requested resource not found */
    GAME_ERR_NOT_CONNECTED    = -4,  /* Network operation on closed socket */
    GAME_ERR_TIMEOUT          = -5,  /* Operation timed out */
    GAME_ERR_SCRIPT           = -6,  /* Lua script compilation/runtime error */
    GAME_ERR_NETWORK          = -7,  /* Network-level failure (connect, DNS) */
    GAME_ERR_ALREADY_EXISTS   = -8,  /* Resource already created/connected */
    GAME_ERR_OUT_OF_MEMORY    = -9,  /* Memory allocation failed */
} game_error_t;

/* =========================================================================
 * Opaque handle types
 * ========================================================================= */

typedef struct game_client_s     game_client_t;     /* Engine + VM instance      */
typedef struct game_tcp_conn_s   game_tcp_conn_t;   /* TCP connection (server)   */
typedef struct game_net_client_s game_net_client_t; /* TCP client connection     */
typedef struct game_net_server_s game_net_server_t; /* TCP server                */
typedef struct game_udp_client_s game_udp_client_t; /* UDP client                */
typedef struct game_udp_server_s game_udp_server_t; /* UDP server                */
typedef struct game_kcp_client_s game_kcp_client_t; /* KCP client                */
typedef struct game_kcp_server_s game_kcp_server_t; /* KCP server                */

/* =========================================================================
 * Callback typedefs
 * ========================================================================= */

/* --- Generic callbacks --- */

/** Called when a one-shot or periodic timer fires. */
typedef void (*game_timer_cb_t)(int timer_id, void* userdata);

/* --- TCP client callbacks --- */

/** Called when a TCP client connection is established (or re-established). */
typedef void (*game_tcp_connect_cb_t)(game_net_client_t* cli, void* userdata);

/** Called when data arrives on a TCP connection.
 *  @param data      received bytes (NOT null-terminated; use data_len)
 *  @param data_len  number of bytes received
 */
typedef void (*game_tcp_message_cb_t)(game_net_client_t* cli,
                                      const char* data, int data_len,
                                      void* userdata);

/** Called when a TCP connection is closed (peer disconnect, error).
 *  NOT called on manual disconnect().
 */
typedef void (*game_tcp_close_cb_t)(game_net_client_t* cli, void* userdata);

/* --- TCP server callbacks --- */

/** Called when a new client connects to a TCP server.
 *  @param conn        handle for this connection (valid until closed)
 *  @param remote_addr peer address string ("IP:port")
 */
typedef void (*game_tcp_server_connect_cb_t)(game_tcp_conn_t* conn,
                                             const char* remote_addr,
                                             void* userdata);

/** Server-level default: called when a connection receives data. */
typedef void (*game_tcp_server_message_cb_t)(game_tcp_conn_t* conn,
                                             const char* data, int data_len,
                                             void* userdata);

/** Server-level default: called when a connection closes. */
typedef void (*game_tcp_server_close_cb_t)(game_tcp_conn_t* conn,
                                           const char* remote_addr,
                                           void* userdata);

/* --- Per-connection callbacks (override server defaults) --- */

typedef void (*game_tcp_conn_message_cb_t)(game_tcp_conn_t* conn,
                                           const char* data, int data_len,
                                           void* userdata);

typedef void (*game_tcp_conn_close_cb_t)(game_tcp_conn_t* conn,
                                         const char* remote_addr,
                                         void* userdata);

/* --- HTTP callbacks --- */

/** Called when an async HTTP request completes or times out.
 *  @param http_code  HTTP status code (200, 404, …); 0 on error/timeout
 *  @param body       response body (NOT null-terminated; use body_len)
 *  @param body_len   response body length; 0 on error/timeout
 */
typedef void (*game_http_response_cb_t)(int http_code,
                                        const char* body, int body_len,
                                        void* userdata);

/* --- UDP callbacks --- */

/** Called for each received UDP datagram.
 *  @param remote_ip  sender IP address (string)
 */
typedef void (*game_udp_message_cb_t)(const char* data, int data_len,
                                      const char* remote_ip,
                                      void* userdata);

/* --- KCP callbacks --- */

/** Called for each received KCP packet.
 *  @param conv  KCP conversation ID that the packet belongs to
 */
typedef void (*game_kcp_message_cb_t)(const char* data, int data_len,
                                      const char* remote_ip, uint32_t conv,
                                      void* userdata);

/* =========================================================================
 * Engine lifecycle
 * ========================================================================= */

/** Create a client engine instance (allocates resources, creates Lua VM).
 *  Must be called once. Only one instance per process is supported
 *  (the underlying C++ Engine is a singleton).
 *
 *  @param out_client  [out] receives the new engine handle
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_client_create(game_client_t** out_client);

/** Initialise the engine: loads config, starts event loop, registers all
 *  Lua bindings, runs init scripts from the scripts directory.
 *
 *  @param client      engine handle from game_client_create
 *  @param config_dir  path to the config directory containing engine.json
 *                     and server.json; pass NULL to use defaults.
 *  @return GAME_OK on success, or GAME_ERR_* on failure
 */
CLIENT_API game_error_t game_client_init(game_client_t* client,
                                              const char* config_dir);

/** Advance the engine by one frame: processes pending events, runs timers,
 *  dispatches network callbacks, calls Lua UpdateScript().
 *
 *  Call this once per game frame from your main/game thread.
 *
 *  @param client  engine handle
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_client_tick(game_client_t* client);

/** Start the engine's own event loop (standalone mode — blocks until
 *  game_client_stop is called from a signal/callback).
 *
 *  For game engine integration, use game_client_tick() instead.
 *
 *  @param client  engine handle
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_client_run(game_client_t* client);

/** Signal the engine to stop. Safe to call from any thread.
 *
 *  @param client  engine handle
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_client_stop(game_client_t* client);

/** Shut down and deallocate the engine. Stops event loop, destroys Lua VM,
 *  releases all network resources. All handles obtained from this engine
 *  become invalid after this call.
 *
 *  @param client  engine handle to destroy (pointer is zeroed)
 */
CLIENT_API void game_client_destroy(game_client_t** client);

/** Query whether the engine is currently running.
 *
 *  @return true if engine is initialised and running
 */
CLIENT_API bool game_client_is_running(game_client_t* client);

/* =========================================================================
 * Script execution
 * ========================================================================= */

/** Execute a Lua string.
 *
 *  @param client      engine handle
 *  @param script      Lua source code (null-terminated UTF-8)
 *  @param error_out   buffer for error message on failure (may be NULL)
 *  @param error_size  size of error_out buffer in bytes
 *  @return GAME_OK on success, GAME_ERR_SCRIPT on Lua error
 */
CLIENT_API game_error_t game_client_do_string(game_client_t* client,
                                                   const char* script,
                                                   char* error_out,
                                                   int error_size);

/** Execute a Lua file.
 *
 *  @param client      engine handle
 *  @param filename    path to the .lua file (UTF-8)
 *  @param error_out   buffer for error message on failure (may be NULL)
 *  @param error_size  size of error_out buffer in bytes
 *  @return GAME_OK on success, GAME_ERR_SCRIPT on Lua error
 */
CLIENT_API game_error_t game_client_do_file(game_client_t* client,
                                                 const char* filename,
                                                 char* error_out,
                                                 int error_size);

/** Register a C function as a Lua global.
 *
 *  The function receives the lua_State* and returns the number of return
 *  values pushed onto the Lua stack (standard Lua C API convention).
 *
 *  Example:
 *      int my_func(lua_State* L) {
 *          const char* s = luaL_checkstring(L, 1);
 *          lua_pushfstring(L, "hello %s", s);
 *          return 1;
 *      }
 *      game_client_register_function(cli, "greet", my_func);
 *
 *  @param client  engine handle
 *  @param name    global name in Lua
 *  @param func    lua_CFunction pointer
 *  @return GAME_OK on success
 */
typedef int (*game_lua_cfunction_t)(void* lua_state);

CLIENT_API game_error_t game_client_register_function(
    game_client_t* client, const char* name, game_lua_cfunction_t func);

/* =========================================================================
 * Logging
 * ========================================================================= */

/** Log messages through the engine's async logger (Quill backend).
 *  These are safe to call from any thread. Messages are formatted with
 *  a "[capi]" prefix to distinguish them from internal engine logs.
 */

CLIENT_API void game_log_trace(game_client_t* client, const char* msg);
CLIENT_API void game_log_debug(game_client_t* client, const char* msg);
CLIENT_API void game_log_info (game_client_t* client, const char* msg);
CLIENT_API void game_log_warn (game_client_t* client, const char* msg);
CLIENT_API void game_log_error(game_client_t* client, const char* msg);
CLIENT_API void game_log_fatal(game_client_t* client, const char* msg);

/* =========================================================================
 * Timer
 * ========================================================================= */

/** Create a one-shot timer. The callback fires once after delay_ms and
 *  the timer is automatically destroyed.
 *
 *  @param client     engine handle
 *  @param delay_ms   delay in milliseconds (>= 1)
 *  @param cb         callback function
 *  @param userdata   user pointer passed to callback
 *  @param out_id     [out] receives the timer ID (for cancellation)
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_timer_timeout(game_client_t* client,
                                                int64_t delay_ms,
                                                game_timer_cb_t cb,
                                                void* userdata,
                                                int* out_id);

/** Create a periodic timer. The callback fires every interval_ms until
 *  cancelled.
 *
 *  @param client      engine handle
 *  @param interval_ms interval in milliseconds (>= 1)
 *  @param cb          callback function
 *  @param userdata    user pointer passed to callback
 *  @param out_id      [out] receives the timer ID (for cancellation)
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_timer_interval(game_client_t* client,
                                                 int64_t interval_ms,
                                                 game_timer_cb_t cb,
                                                 void* userdata,
                                                 int* out_id);

/** Cancel a timer. Safe to call from inside the timer's own callback.
 *
 *  @param client    engine handle
 *  @param timer_id  ID returned by game_timer_timeout / game_timer_interval
 *  @return GAME_OK on success, GAME_ERR_NOT_FOUND if timer doesn't exist
 */
CLIENT_API game_error_t game_timer_cancel(game_client_t* client,
                                               int timer_id);

/* =========================================================================
 * TCP Client
 * ========================================================================= */

/** Connect to a TCP server asynchronously.
 *
 *  The returned handle is valid immediately, but the connection is not
 *  established until the on_connect callback fires. Data sent before
 *  on_connect is queued by the OS (TCP buffer).
 *
 *  @param client      engine handle
 *  @param addr        address in "host:port" format ("127.0.0.1:8080")
 *  @param out_conn    [out] receives the client handle
 *  @return GAME_OK on success (handle created, connecting in background)
 */
CLIENT_API game_error_t game_tcp_connect(game_client_t* client,
                                              const char* addr,
                                              game_net_client_t** out_conn);

/** Send data on a TCP connection.
 *
 *  @param conn      client handle
 *  @param data      bytes to send (may contain embedded NULs)
 *  @param data_len  number of bytes to send
 *  @return GAME_OK on success, GAME_ERR_NOT_CONNECTED if disconnected
 */
CLIENT_API game_error_t game_tcp_send(game_net_client_t* conn,
                                           const char* data, int data_len);

/** Disconnect and release a TCP client.
 *  Does NOT trigger the on_close callback.
 *
 *  @param conn  client handle (pointer is zeroed on success)
 */
CLIENT_API void game_tcp_disconnect(game_net_client_t** conn);

/** Check whether the TCP connection is active.
 *
 *  @return true if connected and ready for send/recv
 */
CLIENT_API bool game_tcp_is_connected(game_net_client_t* conn);

/** Set the on_connect callback. Call before or right after game_tcp_connect.
 *
 *  @param conn      client handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_set_on_connect(game_net_client_t* conn,
                                             game_tcp_connect_cb_t cb,
                                             void* userdata);

/** Set the on_message callback.
 *
 *  @param conn      client handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_set_on_message(game_net_client_t* conn,
                                             game_tcp_message_cb_t cb,
                                             void* userdata);

/** Set the on_close callback. NOT triggered by game_tcp_disconnect().
 *
 *  @param conn      client handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_set_on_close(game_net_client_t* conn,
                                           game_tcp_close_cb_t cb,
                                           void* userdata);

/* =========================================================================
 * TCP Server
 * ========================================================================= */

/** Start listening for TCP connections.
 *
 *  @param client      engine handle
 *  @param addr        bind address in "host:port" format ("0.0.0.0:8080")
 *  @param out_server  [out] receives the server handle
 *  @return GAME_OK on success, GAME_ERR_NETWORK on bind/listen failure
 */
CLIENT_API game_error_t game_tcp_listen(game_client_t* client,
                                             const char* addr,
                                             game_net_server_t** out_server);

/** Stop the TCP server and release all connections.
 *
 *  @param server  server handle (pointer is zeroed on success)
 */
CLIENT_API void game_tcp_server_stop(game_net_server_t** server);

/** Set the server-level on_connect callback. Fires for each new connection.
 *
 *  @param server    server handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_server_set_on_connect(
    game_net_server_t* server, game_tcp_server_connect_cb_t cb,
    void* userdata);

/** Set the server-level on_message callback. Used when a connection does
 *  not have its own on_message callback set.
 *
 *  @param server    server handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_server_set_on_message(
    game_net_server_t* server, game_tcp_server_message_cb_t cb,
    void* userdata);

/** Set the server-level on_close callback. Used when a connection does
 *  not have its own on_close callback set.
 *
 *  @param server    server handle
 *  @param cb        callback (NULL to clear)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_server_set_on_close(
    game_net_server_t* server, game_tcp_server_close_cb_t cb,
    void* userdata);

/* --- Per-connection operations (on handles received in on_connect) --- */

/** Send data to a specific TCP connection.
 *
 *  @param conn      connection handle (from on_connect callback)
 *  @param data      bytes to send
 *  @param data_len  number of bytes to send
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_tcp_conn_send(game_tcp_conn_t* conn,
                                                const char* data,
                                                int data_len);

/** Close a single connection. Does NOT trigger on_close.
 *
 *  @param conn  connection handle (pointer is zeroed on success)
 */
CLIENT_API void game_tcp_conn_close(game_tcp_conn_t** conn);

/** Check whether a connection is active.
 *
 *  @return true if still connected
 */
CLIENT_API bool game_tcp_conn_is_connected(game_tcp_conn_t* conn);

/** Set a per-connection on_message callback (overrides server default).
 *
 *  @param conn      connection handle
 *  @param cb        callback (NULL to revert to server default)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_conn_set_on_message(
    game_net_server_t* server, game_tcp_conn_t* conn,
    game_tcp_conn_message_cb_t cb, void* userdata);

/** Set a per-connection on_close callback (overrides server default).
 *  NOT triggered by game_tcp_conn_close().
 *
 *  @param conn      connection handle
 *  @param cb        callback (NULL to revert to server default)
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_tcp_conn_set_on_close(
    game_net_server_t* server, game_tcp_conn_t* conn,
    game_tcp_conn_close_cb_t cb, void* userdata);

/* =========================================================================
 * HTTP Client
 * ========================================================================= */

/** Send an async HTTP GET request.
 *
 *  The callback fires on the event-loop thread (same thread as tick()).
 *  On timeout or error, http_code is 0 and body is empty.
 *
 *  @param client    engine handle
 *  @param url       request URL ("https://example.com/api")
 *  @param cb        response callback
 *  @param userdata  user pointer passed to callback
 *  @return GAME_OK on success (request submitted)
 */
CLIENT_API game_error_t game_http_get(game_client_t* client,
                                           const char* url,
                                           game_http_response_cb_t cb,
                                           void* userdata);

/** Send an async HTTP POST request.
 *
 *  @param client    engine handle
 *  @param url       request URL
 *  @param body      request body (may be NULL for empty body)
 *  @param body_len  request body length in bytes
 *  @param cb        response callback
 *  @param userdata  user pointer passed to callback
 *  @return GAME_OK on success (request submitted)
 */
CLIENT_API game_error_t game_http_post(game_client_t* client,
                                            const char* url,
                                            const char* body, int body_len,
                                            game_http_response_cb_t cb,
                                            void* userdata);

/* =========================================================================
 * UDP Client
 * ========================================================================= */

/** Create a UDP client and connect to a remote endpoint.
 *  UDP is connectionless; "connect" here just sets the default destination
 *  for send().
 *
 *  @param client     engine handle
 *  @param host       remote IP address ("127.0.0.1")
 *  @param port       remote port (1–65535)
 *  @param out_udp    [out] receives the UDP client handle
 *  @return GAME_OK on success, GAME_ERR_NETWORK on failure
 */
CLIENT_API game_error_t game_udp_connect(game_client_t* client,
                                              const char* host, int port,
                                              game_udp_client_t** out_udp);

/** Send a UDP datagram to the connected endpoint.
 *
 *  @param udp       UDP client handle
 *  @param data      bytes to send
 *  @param data_len  number of bytes
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_udp_send(game_udp_client_t* udp,
                                           const char* data, int data_len);

/** Synchronous UDP request-response.
 *  Sends data and blocks until a response is received or timeout.
 *
 *  @param udp          UDP client handle
 *  @param data         request data
 *  @param data_len     request data length
 *  @param timeout_ms   max wait time in milliseconds
 *  @param resp_buf     buffer for response data (caller-allocated)
 *  @param resp_cap     capacity of resp_buf in bytes
 *  @param out_resp_len [out] actual response length written
 *  @return GAME_OK on success, GAME_ERR_TIMEOUT if no response
 */
CLIENT_API game_error_t game_udp_request(game_udp_client_t* udp,
                                              const char* data, int data_len,
                                              int timeout_ms,
                                              char* resp_buf, int resp_cap,
                                              int* out_resp_len);

/** One-shot UDP request-response (no persistent connection).
 *
 *  @param client       engine handle
 *  @param host         remote IP
 *  @param port         remote port
 *  @param data         request data
 *  @param data_len     request data length
 *  @param timeout_ms   max wait time in milliseconds
 *  @param resp_buf     buffer for response data
 *  @param resp_cap     capacity of resp_buf
 *  @param out_resp_len [out] actual response length
 *  @return GAME_OK on success, GAME_ERR_TIMEOUT on timeout
 */
CLIENT_API game_error_t game_udp_request_to(game_client_t* client,
                                                 const char* host, int port,
                                                 const char* data, int data_len,
                                                 int timeout_ms,
                                                 char* resp_buf, int resp_cap,
                                                 int* out_resp_len);

/** One-shot UDP send (fire-and-forget). No response expected.
 *
 *  @param client     engine handle
 *  @param host       remote IP
 *  @param port       remote port
 *  @param data       bytes to send
 *  @param data_len   number of bytes
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_udp_send_to(game_client_t* client,
                                              const char* host, int port,
                                              const char* data, int data_len);

/** Close and release a UDP client.
 *
 *  @param udp  UDP client handle (pointer is zeroed)
 */
CLIENT_API void game_udp_close(game_udp_client_t** udp);

/** Check whether the UDP client has an active socket.
 *
 *  @return true if connected
 */
CLIENT_API bool game_udp_is_connected(game_udp_client_t* udp);

/* =========================================================================
 * UDP Server
 * ========================================================================= */

/** Start a UDP server listening on a port.
 *
 *  @param client     engine handle
 *  @param port       local port (1–65535)
 *  @param cb         callback for received datagrams (may be NULL)
 *  @param userdata   user pointer passed to callback
 *  @param out_udp    [out] receives the UDP server handle
 *  @return GAME_OK on success, GAME_ERR_NETWORK on bind failure
 */
CLIENT_API game_error_t game_udp_listen(game_client_t* client,
                                             int port,
                                             game_udp_message_cb_t cb,
                                             void* userdata,
                                             game_udp_server_t** out_udp);

/** Stop the UDP server and release resources.
 *
 *  @param server  server handle (pointer is zeroed)
 */
CLIENT_API void game_udp_server_stop(game_udp_server_t** server);

/** Pause receiving datagrams (server stays alive, queued messages still
 *  dispatch).
 */
CLIENT_API void game_udp_server_pause(game_udp_server_t* server);

/** Resume receiving datagrams after a pause. */
CLIENT_API void game_udp_server_resume(game_udp_server_t* server);

/** Check if the UDP server is running.
 *
 *  @return true if running and accepting datagrams
 */
CLIENT_API bool game_udp_server_is_running(game_udp_server_t* server);

/** Replace the on_message callback atomically.
 *
 *  @param server    server handle
 *  @param cb        new callback
 *  @param userdata  user pointer passed to callback
 */
CLIENT_API void game_udp_server_set_on_message(game_udp_server_t* server,
                                                    game_udp_message_cb_t cb,
                                                    void* userdata);

/* =========================================================================
 * KCP Client (reliable UDP)
 * ========================================================================= */

/** Create a KCP client and connect to a remote KCP server.
 *
 *  @param client     engine handle
 *  @param host       remote IP
 *  @param port       remote port (1–65535)
 *  @param conv       conversation ID (must match server's conv)
 *  @param out_kcp    [out] receives the KCP client handle
 *  @return GAME_OK on success, GAME_ERR_NETWORK on failure
 */
CLIENT_API game_error_t game_kcp_connect(game_client_t* client,
                                              const char* host, int port,
                                              uint32_t conv,
                                              game_kcp_client_t** out_kcp);

/** Create an unconnected KCP client (for parameter tuning before connect).
 *
 *  @param conv       conversation ID
 *  @param out_kcp    [out] receives the KCP client handle
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_kcp_new(uint32_t conv,
                                          game_kcp_client_t** out_kcp);

/** Connect an unconnected KCP client (created via game_kcp_new).
 *
 *  @param kcp   client handle
 *  @param host  remote IP
 *  @param port  remote port
 *  @return GAME_OK on success, GAME_ERR_NETWORK on failure
 */
CLIENT_API game_error_t game_kcp_client_connect(game_kcp_client_t* kcp,
                                                     const char* host,
                                                     int port);

/** Send data over KCP.
 *
 *  @param kcp       client handle
 *  @param data      bytes to send
 *  @param data_len  number of bytes
 *  @return GAME_OK on success
 */
CLIENT_API game_error_t game_kcp_send(game_kcp_client_t* kcp,
                                           const char* data, int data_len);

/** Synchronous KCP request-response.
 *
 *  @param kcp          client handle
 *  @param data         request data
 *  @param data_len     request data length
 *  @param timeout_ms   max wait in milliseconds
 *  @param resp_buf     buffer for response
 *  @param resp_cap     buffer capacity
 *  @param out_resp_len [out] actual response length
 *  @return GAME_OK on success, GAME_ERR_TIMEOUT on timeout
 */
CLIENT_API game_error_t game_kcp_request(game_kcp_client_t* kcp,
                                              const char* data, int data_len,
                                              int timeout_ms,
                                              char* resp_buf, int resp_cap,
                                              int* out_resp_len);

/** Close and release a KCP client.
 *
 *  @param kcp  client handle (pointer is zeroed)
 */
CLIENT_API void game_kcp_close(game_kcp_client_t** kcp);

/** Check if the KCP client is connected.
 *
 *  @return true if connected
 */
CLIENT_API bool game_kcp_is_connected(game_kcp_client_t* kcp);

/* --- KCP parameter tuning (call before connect) --- */

/** Configure KCP nodelay mode.
 *
 *  Common presets:
 *    Normal:   game_kcp_set_nodelay(kcp, 0, 40, 0, 0)
 *    Fast:     game_kcp_set_nodelay(kcp, 1, 10, 2, 1)
 *    Extreme:  game_kcp_set_nodelay(kcp, 2, 10, 2, 1)
 *
 *  @param nodelay   0=disable, 1=enable, 2=enable+flow-control
 *  @param interval  internal update clock interval (ms), typically 10–40
 *  @param resend    fast retransmit threshold, 0=disable, 2=common
 *  @param nc        0=normal congestion control, 1=disable (full speed)
 */
CLIENT_API void game_kcp_set_nodelay(game_kcp_client_t* kcp,
                                          int nodelay, int interval,
                                          int resend, int nc);

/** Set KCP window sizes.
 *
 *  @param sndwnd  send window (packets), default 32
 *  @param rcvwnd  receive window (packets), default 128
 */
CLIENT_API void game_kcp_set_wnd_size(game_kcp_client_t* kcp,
                                           int sndwnd, int rcvwnd);

/** Set KCP MTU (maximum transmission unit).
 *
 *  @param mtu  MTU in bytes, default 1400, range 50–1500
 */
CLIENT_API void game_kcp_set_mtu(game_kcp_client_t* kcp, int mtu);

/** Set KCP conversation ID.
 *
 *  @param conv  conversation ID (0 – 2^32-1)
 */
CLIENT_API void game_kcp_set_conv(game_kcp_client_t* kcp, uint32_t conv);

/* =========================================================================
 * KCP Server (reliable UDP)
 * ========================================================================= */

/** Start a KCP server listening on a port.
 *
 *  @param client     engine handle
 *  @param port       local port (1–65535)
 *  @param cb         callback for received KCP packets
 *  @param userdata   user pointer passed to callback
 *  @param out_kcp    [out] receives the KCP server handle
 *  @return GAME_OK on success, GAME_ERR_NETWORK on bind failure
 */
CLIENT_API game_error_t game_kcp_listen(game_client_t* client,
                                             int port,
                                             game_kcp_message_cb_t cb,
                                             void* userdata,
                                             game_kcp_server_t** out_kcp);

/** Stop the KCP server and release all resources.
 *
 *  @param server  server handle (pointer is zeroed)
 */
CLIENT_API void game_kcp_server_stop(game_kcp_server_t** server);

/** Pause receiving KCP packets. */
CLIENT_API void game_kcp_server_pause(game_kcp_server_t* server);

/** Resume receiving KCP packets. */
CLIENT_API void game_kcp_server_resume(game_kcp_server_t* server);

/** Check if the KCP server is running. */
CLIENT_API bool game_kcp_server_is_running(game_kcp_server_t* server);

/** Replace the on_message callback atomically. */
CLIENT_API void game_kcp_server_set_on_message(
    game_kcp_server_t* server, game_kcp_message_cb_t cb, void* userdata);

/* --- KCP server parameter tuning --- */

CLIENT_API void game_kcp_server_set_nodelay(game_kcp_server_t* server,
                                                 int nodelay, int interval,
                                                 int resend, int nc);

CLIENT_API void game_kcp_server_set_wnd_size(game_kcp_server_t* server,
                                                  int sndwnd, int rcvwnd);

CLIENT_API void game_kcp_server_set_mtu(game_kcp_server_t* server,
                                             int mtu);

/** Set the session timeout. Sessions with no activity for timeout_ms
 *  are automatically cleaned up.
 *
 *  @param timeout_ms  timeout in milliseconds (0 = use default)
 */
CLIENT_API void game_kcp_server_set_session_timeout(
    game_kcp_server_t* server, int timeout_ms);

/* =========================================================================
 * Utility
 * ========================================================================= */

/** Get the last error message for this engine instance.
 *
 *  @param client   engine handle
 *  @param buf      caller-allocated buffer
 *  @param buf_size buffer size in bytes
 *  @return number of bytes written (excluding NUL), or 0 if no error
 */
CLIENT_API int game_client_last_error(game_client_t* client,
                                           char* buf, int buf_size);

/** Get the underlying lua_State* for advanced Lua C API usage.
 *  Use with caution — you are responsible for stack balance.
 *
 *  @return lua_State pointer, or NULL if engine is not initialised
 */
CLIENT_API void* game_client_get_lua_state(game_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* CLIENT_H */
