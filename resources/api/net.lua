--- Network API
--- Global module: net
---
--- Sub-modules:
---   net.client  -- TCP client
---   net.server  -- TCP server
---   net.http    -- HTTP client
---
--- All callbacks are invoked asynchronously on the event loop thread.
--- Callback signature summary (instance methods receive self as first arg):
---   client.on_connect(self)                             -- no additional args
---   client.on_message(self, data: string)               -- raw data
---   client.on_close(self)                               -- no additional args
---   server.on_connect(conn_id: integer, addr: string)   -- new connection
---   server.on_message(conn_id: integer, data: string)   -- server-level default
---   server.on_close(conn_id: integer, addr: string)     -- server-level default
---   connection-level on_message(data: string)           -- overrides server default
---   connection-level on_close(conn_id: integer, addr: string) -- overrides server default
---   http.on_response(http_code: integer, body: string)  -- response/timeout callback

-- ============================================================================
-- net.client -- TCP client (light userdata + Lua class)
-- ============================================================================

--- Create a TCP client and connect to host:port.
--- Returns a class instance with methods and callback slots.
---@param addr string   address in "host:port" format
---@return table client_instance  object with methods: send, disconnect, is_connected
function net.client.connect(addr) end

--- Instance: send data to the connection.
--- Call as client:send(data).
---@param data string  raw data to send
function client:send(data) end

--- Instance: disconnect and release the client.
--- Call as client:disconnect().
---@return boolean existed  true if still active and disconnected, false if already closed
function client:disconnect() end

--- Instance: check whether the connection is active.
--- Call as client:is_connected().
---@return boolean connected
function client:is_connected() end

--- Instance: set or clear the message callback.
--- Call as client:set_on_message(callback).
--- Pass nil or call with no argument to clear.
---@param callback function?  data receive callback: fun(self, data: string)
function client:set_on_message(callback) end

--- Instance: set or clear the close callback.
--- Call as client:set_on_close(callback).
--- Pass nil or call with no argument to clear.
---@param callback function?  connection close callback: fun(self)
function client:set_on_close(callback) end

--- Callback slot: set on the instance to receive connect events.
---   client.on_connect = function(self) ... end
---@param self table  the client instance

--- Callback slot: set on the instance to receive messages.
---   client.on_message = function(self, data) ... end
---@param self table  the client instance
---@param data string  received raw data

--- Callback slot: set on the instance to receive close events.
---   client.on_close = function(self) ... end
---@param self table  the client instance

-- ============================================================================
-- net.server -- TCP server
-- ============================================================================

--- Create and start a TCP server, listening on host:port.
---@param addr        string   address in "host:port" format
---@param on_connect? function new connection callback: fun(conn_id: integer, remote_addr: string)
---@param on_message? function server-level data callback: fun(conn_id: integer, data: string)
---@param on_close?   function server-level close callback: fun(conn_id: integer, remote_addr: string)
---@return integer server_id  success: server identifier
---@return nil, string errmsg  failure: error message
function net.server.listen(addr, on_connect, on_message, on_close) end

--- Send data to the specified connection.
---@param conn_id integer  connection id
---@param data    string   raw data to send
function net.server.send(conn_id, data) end

--- Close the specified connection and release its callbacks.
---@param conn_id integer
---@return boolean closed  true if found and closed, false if not found
function net.server.close_conn(conn_id) end

--- Stop the server, releasing all connections and callbacks.
---@param server_id integer
---@return boolean stopped  true if found and stopped, false if not found
function net.server.stop(server_id) end

--- Set a per-connection message callback, overriding the server default on_message.
--- Pass nil to remove the per-connection callback and revert to the server default.
---@param conn_id  integer   connection id
---@param callback function?  data receive callback: fun(data: string)
function net.server.set_on_message(conn_id, callback) end

--- Set a per-connection close callback, overriding the server default on_close.
--- Pass nil to remove the per-connection callback and revert to the server default.
---@param conn_id  integer   connection id
---@param callback function?  connection close callback: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_close(conn_id, callback) end

--- Replace the server's on_connect callback.
---@param server_id integer   server id
---@param callback  function?  new connection callback: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_connect(server_id, callback) end

--- Replace the server's on_close callback.
---@param server_id integer   server id
---@param callback  function?  connection close callback: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_disconnect(server_id, callback) end

-- ============================================================================
-- net.http -- HTTP client
-- ============================================================================

--- Send an async HTTP GET request (10s timeout).
---@param url         string   request URL
---@param on_response function response callback: fun(http_code: integer, body: string)
---                             on error/timeout, http_code is 0 and body is ""
function net.http.get(url, on_response) end

--- Send an async HTTP POST request (10s timeout).
---@param url         string   request URL
---@param body        string   request body
---@param on_response function response callback: fun(http_code: integer, body: string)
function net.http.post(url, body, on_response) end
