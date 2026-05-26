-- net/init.lua
-- Net module — re-exports TcpClient and TcpServer wrapper classes.
--
-- Usage:
--   local TcpClient = import("runtime.net.client")
--   local TcpServer = import("runtime.net.server")

return {
    client = import("runtime.net.client"),
    server = import("runtime.net.server"),
}
