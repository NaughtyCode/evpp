--- Network API
--- Global module: net
---
--- Sub-modules:
---   net.client      -- TCP client (light userdata + Lua class instance)
---   net.server      -- TCP server (light userdata + Lua class instances)
---   net.http        -- HTTP client (async callback-based)
---   net.udp_client  -- UDP client (synchronous, blocking)
---   net.udp_server  -- UDP server (async callback-based)
---
--- All callbacks are invoked asynchronously on the event loop thread.
--- Never block or sleep inside a callback.

-- ============================================================================
-- net.client -- TCP client
-- ============================================================================
-- Each client returned by net.client.connect() is a Lua class instance
-- (table with metatable).  The C++ context is stored as light userdata in
-- the _ctx field and cleaned up by Lua GC (__gc metamethod) or explicitly
-- via :disconnect().
--
-- Methods:
--   client:send(data)               send raw data       (raises if closed)
--   client:disconnect() -> bool     disconnect & release (idempotent)
--   client:is_connected() -> bool   check liveness
--   client:set_on_message(fn)       set or clear on_message
--   client:set_on_close(fn)         set or clear on_close
--
-- Callback slots (set directly on the instance table):
--   client.on_connect = function(self) ... end
--   client.on_message = function(self, data: string) ... end
--   client.on_close   = function(self) ... end

--- Create a TCP client and connect to host:port.
---@param addr string   address in "host:port" format
---@return table client  instance with methods and callback slots
function net.client.connect(addr) end

--- Send raw data to the connection.
--- Must be connected; raises an error if the client is closed or disconnected.
---@param data string
function client:send(data) end

--- Disconnect and release the client.  Safe to call multiple times.
---@return boolean existed  true if still active, false if already closed
function client:disconnect() end

--- Check whether the connection is currently active.
---@return boolean connected
function client:is_connected() end

--- Set or clear the on_message callback.
--- Pass nil or no argument to clear.
---@param callback fun(self: table, data: string)?
function client:set_on_message(callback) end

--- Set or clear the on_close callback.
--- Pass nil or no argument to clear.
---@param callback fun(self: table)?
function client:set_on_close(callback) end

-- ============================================================================
-- net.server -- TCP server
-- ============================================================================
-- net.server.listen() returns a Lua class instance (table with metatable).
-- The C++ context is stored as light userdata in _ctx; cleanup via __gc or
-- explicit :stop().
--
-- Each new connection is itself a Lua class instance passed to the on_connect
-- callback.  The connection's C++ context is stored in TCPConn internals (not
-- a hash table), so dispatch is O(1).
--
-- Callback dispatch order (per-connection overrides server-wide):
--   on_message → conn.on_message  >  server.on_message
--   on_close   → conn.on_close    >  server.on_close
--
-- Server methods:
--   server:stop() -> bool            stop server, release all connections
--   server:set_on_connect(fn)        set or clear on_connect callback
--   server:set_on_close(fn)          set or clear server-wide on_close
--
-- Server callback slots (set directly on the server instance):
--   server.on_connect = function(self, conn, remote_addr) ... end
--   server.on_message = function(self, conn, data) ... end    -- server-wide fallback
--   server.on_close   = function(self, conn, remote_addr) ... end
--
-- Connection methods:
--   conn:send(data)                  send raw data   (raises if closed)
--   conn:close() -> bool             disconnect      (idempotent)
--   conn:set_on_message(fn)          set or clear on_message
--   conn:set_on_close(fn)            set or clear on_close
--
-- Connection callback slots (set directly on the conn instance):
--   conn.on_message = function(self, data) ... end
--   conn.on_close   = function(self, remote_addr) ... end

--- Create and start a TCP server listening on host:port.
--- Set callbacks on the returned server instance before any connections
--- arrive (the event loop delivers callbacks asynchronously).
---@param addr string   address in "host:port" format
---@return table server  instance with methods and callback slots
function net.server.listen(addr) end

--- Stop the server, closing all connections and releasing resources.
---@return boolean existed  true if still active, false if already stopped
function server:stop() end

--- Set or clear the server-wide on_connect callback.
--- Pass nil or no argument to clear.
---@param callback fun(self: table, conn: table, remote_addr: string)?
function server:set_on_connect(callback) end

--- Set or clear the server-wide on_close callback (fallback for connections
--- without a per-connection on_close).
--- Pass nil or no argument to clear.
---@param callback fun(self: table, conn: table, remote_addr: string)?
function server:set_on_close(callback) end

--- Send raw data through the connection.
--- Must be connected; raises an error if closed or disconnected.
---@param data string
function conn:send(data) end

--- Close the connection.  Does NOT fire on_close for manual close.
---@return boolean existed  true if still active, false if already closed
function conn:close() end

--- Set or clear the per-connection on_message callback, overriding the
--- server-wide on_message for this connection.
--- Pass nil or no argument to remove and revert to server-wide.
---@param callback fun(self: table, data: string)?
function conn:set_on_message(callback) end

--- Set or clear the per-connection on_close callback, overriding the
--- server-wide on_close for this connection.
--- Pass nil or no argument to remove and revert to server-wide.
---@param callback fun(self: table, remote_addr: string)?
function conn:set_on_close(callback) end

-- ============================================================================
-- net.http -- HTTP client
-- ============================================================================
-- Async HTTP requests with callback-based response handling.
-- Pending callbacks are automatically released on engine shutdown;
-- callbacks that fire after shutdown receive no invocation.
--
-- Error handling: on error or timeout, the callback receives
-- http_code = 0 and body = "".

--- Send an async HTTP GET request.
---@param url         string   request URL
---@param on_response fun(http_code: integer, body: string)  response callback
function net.http.get(url, on_response) end

--- Send an async HTTP POST request.
---@param url         string   request URL
---@param body        string   request body
---@param on_response fun(http_code: integer, body: string)  response callback
function net.http.post(url, body, on_response) end

-- ============================================================================
-- net.udp_client -- UDP client (synchronous)
-- ============================================================================
-- Wraps evpp::udp::sync::Client.  All operations are synchronous and will
-- block the Lua VM until completion.  Suitable for testing and one-shot
-- requests, not for production async workloads.
--
-- Each client returned by net.udp_client.connect() is a Lua class instance
-- (table with metatable).  The C++ context is stored as light userdata in
-- the _ctx field and cleaned up by Lua GC (__gc metamethod) or explicitly
-- via :close().
--
-- Instance methods:
--   udp_client:send(data) -> bool
--   udp_client:do_request(data [, timeout_ms]) -> string
--   udp_client:close() -> bool
--   udp_client:is_connected() -> bool
--
-- Static helpers (no instance needed):
--   net.udp_client.do_request(host, port, data [, timeout_ms]) -> string
--   net.udp_client.send_to(host, port, data) -> bool

--- Create a UDP client and connect to host:port.
---@param host string
---@param port integer
---@return table client   instance with methods
---@return nil, string    error message on failure
function net.udp_client.connect(host, port) end

--- Send raw data through the UDP socket.
---@param data string
---@return boolean ok
function udp_client:send(data) end

--- Send data and wait for a response (blocking).
---@param data        string
---@param timeout_ms? integer  default 3000
---@return string response  empty string on failure
function udp_client:do_request(data, timeout_ms) end

--- Close the UDP socket and release the client.
---@return boolean existed  true if still active, false if already closed
function udp_client:close() end

--- Check whether the client is active.
---@return boolean connected
function udp_client:is_connected() end

--- One-shot: connect, send, wait for response, disconnect.
--- Creates a temporary UDP client internally.
---@param host        string
---@param port        integer
---@param data        string
---@param timeout_ms? integer  default 3000
---@return string response  empty string on failure
function net.udp_client.do_request(host, port, data, timeout_ms) end

--- One-shot: resolve host, send data, close.
--- Creates a temporary UDP client internally.
---@param host string
---@param port integer
---@param data string
---@return boolean ok
function net.udp_client.send_to(host, port, data) end

-- ============================================================================
-- net.udp_server -- UDP server (async)
-- ============================================================================
-- Wraps evpp::udp::Server.  Messages are received on internal recv threads
-- and dispatched to the main EventLoop thread for Lua callback invocation.
--
-- Each server returned by net.udp_server.listen() is a Lua class instance
-- (table with metatable).  The C++ context is stored as light userdata in
-- the _ctx field and cleaned up by Lua GC (__gc metamethod) or explicitly
-- via :stop().
--
-- Unlike TCP, UDP is connectionless — there are no per-connection state or
-- callbacks.  Each message delivers (data, remote_ip) to the on_message
-- callback.
--
-- Instance methods:
--   server:stop() -> bool
--   server:pause()
--   server:continue()
--   server:is_running() -> bool
--   server:set_on_message(callback)

--- Create and start a UDP server listening on port(s).
--- Accepts a single port integer, or a port string like "53,5353".
---@param port_or_ports integer|string  single port or comma-separated list
---@param on_message    fun(data: string, remote_ip: string)?  message callback
---@return table server  instance with methods
---@return nil, string   error message on failure
function net.udp_server.listen(port_or_ports, on_message) end

--- Stop the UDP server and release its callback.
---@return boolean stopped  true if still active, false if already stopped
function server:stop() end

--- Pause message receiving (no callbacks will fire).
function server:pause() end

--- Resume message receiving after a pause.
function server:continue() end

--- Check whether the server is currently running.
---@return boolean running
function server:is_running() end

--- Replace the on_message callback for a running server.
--- Pass nil or no argument to remove the callback.
---@param callback  fun(data: string, remote_ip: string)?
function server:set_on_message(callback) end
