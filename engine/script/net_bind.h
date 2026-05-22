#pragma once

namespace engine {

class ScriptVM;

namespace script {

// Export the "net" module to Lua with the following API:
//
//   -- TCP Client
//   net.client.connect(host_port, on_connect, on_message, on_close)
//       → client_id or nil, errmsg
//   net.client.send(client_id, data)
//   net.client.disconnect(client_id)
//   net.client.is_connected(client_id) → bool
//   net.client.set_on_message(client_id, callback)  -- update message handler
//   net.client.set_on_close(client_id, callback)    -- update close handler
//
//   -- TCP Server
//   net.server.listen(host_port, on_connect, on_message, on_close)
//       → server_id or nil, errmsg
//   net.server.send(conn_id, data)
//   net.server.close_conn(conn_id)
//   net.server.stop(server_id)
//   net.server.set_on_message(conn_id, callback)    -- per-connection message handler
//   net.server.set_on_close(conn_id, callback)      -- per-connection close handler
//   net.server.set_on_connect(server_id, callback)  -- update server-wide on_connect
//   net.server.set_on_disconnect(server_id, callback) -- update server-wide on_close
//
//   -- HTTP Client
//   net.http.get(url, on_response)    -- on_response(code, body)
//   net.http.post(url, body, on_response)
//
void ExportNet(ScriptVM& vm);

// Cancel all network objects and release Lua function references.
void ShutdownNetBindings();

} // namespace script
} // namespace engine
