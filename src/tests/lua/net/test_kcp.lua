-- test_kcp.lua
-- Basic KCP client/server test via Lua bindings.

local t = import("tests.harness.test_harness")

t.start("KCP server start and stop")
local server = net.kcp_server.listen(21235)
t.assert_truthy(server ~= nil, "KCP server created")

t.assert_truthy(server:is_running(), "server is running")

server:stop()
t.assert_truthy(not server:is_running(), "server stopped")

t.summary()
