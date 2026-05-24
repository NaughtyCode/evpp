--- Network API
--- Global module: net
---
--- Sub-modules:
---   net.client  -- TCP client (light userdata + Lua class instance)
---   net.server  -- TCP server (integer handles: server_id, conn_id)
---   net.http    -- HTTP client (async callback-based)
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
-- Servers are identified by an integer server_id returned from listen().
-- Connections are identified by an integer conn_id passed to callbacks.
-- Servers must be explicitly stopped via net.server.stop(); connections
-- are cleaned up automatically when they disconnect.
--
-- Callback dispatch order (per-connection overrides server-wide):
--   on_message → per-connection set_on_message  >  server-wide on_message
--   on_close   → per-connection set_on_close    >  server-wide on_close

--- Create and start a TCP server listening on host:port.
--- The optional callbacks are server-wide defaults.
---@param addr        string    address in "host:port" format
---@param on_connect? fun(conn_id: integer, remote_addr: string)  new connection
---@param on_message? fun(conn_id: integer, data: string)         server-wide message
---@param on_close?   fun(conn_id: integer, remote_addr: string)  server-wide close
---@return integer server_id   success: server identifier
---@return nil, string errmsg  failure: error message
function net.server.listen(addr, on_connect, on_message, on_close) end

--- Send raw data to a connection.
---@param conn_id integer
---@param data    string
function net.server.send(conn_id, data) end

--- Close a connection and release its per-connection callbacks.
---@param conn_id integer
---@return boolean closed  true if found and closed, false if not found
function net.server.close_conn(conn_id) end

--- Stop the server, releasing all connections and callbacks.
---@param server_id integer
---@return boolean stopped  true if found and stopped, false if not found
function net.server.stop(server_id) end

--- Set a per-connection on_message callback, overriding the server default.
--- Pass nil or no argument to remove and revert to the server default.
---@param conn_id  integer
---@param callback fun(data: string)?
function net.server.set_on_message(conn_id, callback) end

--- Set a per-connection on_close callback, overriding the server default.
--- Pass nil or no argument to remove and revert to the server default.
---@param conn_id  integer
---@param callback fun(conn_id: integer, remote_addr: string)?
function net.server.set_on_close(conn_id, callback) end

--- Replace the server-wide on_connect callback.
---@param server_id integer
---@param callback  fun(conn_id: integer, remote_addr: string)?
function net.server.set_on_connect(server_id, callback) end

--- Replace the server-wide on_close callback (fallback for connections
--- without a per-connection on_close).
---@param server_id integer
---@param callback  fun(conn_id: integer, remote_addr: string)?
function net.server.set_on_disconnect(server_id, callback) end

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
