-- test_kcp.lua
-- Basic KCP client/server test via Lua bindings.

local t = import("tests.harness.test_harness")

t.start("KCP server start, request, and stop")
local server = net.kcp_server.listen(21235, function(data, remote_ip, conv)
    t.assert_truthy(remote_ip ~= nil and remote_ip ~= "", "remote ip supplied")
    t.assert_eq(conv, 0x11223344, "conv supplied")
    return "ECHO:" .. data
end)
t.assert_truthy(server ~= nil, "KCP server created")

t.assert_truthy(server:is_running(), "server is running")

local response = net.kcp_client.do_request("127.0.0.1", 21235, "hello_kcp", 3000)
t.assert_eq(response, "ECHO:hello_kcp", "KCP request receives reliable response")

server:pause()
t.assert_truthy(not server:is_running(), "server paused")
server:resume()
t.assert_truthy(server:is_running(), "server resumed")

server:stop()
t.assert_truthy(not server:is_running(), "server stopped")

t.summary()
