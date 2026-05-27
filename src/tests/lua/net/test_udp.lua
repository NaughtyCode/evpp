-- test_udp.lua
-- Basic UDP client/server test via Lua bindings.

local t = import("tests.harness.test_harness")

t.start("UDP server start and stop")
local server = net.udp_server.listen(21234)
t.assert_truthy(server ~= nil, "UDP server created")

t.assert_truthy(server:is_running(), "server is running")

server:stop()
t.assert_truthy(not server:is_running(), "server stopped")

t.summary()
