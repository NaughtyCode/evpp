#include <catch2/catch_test_macros.hpp>

#include <string>

#include "runtime/config/config.h"
#include "log_init.h"
#include "runtime/network/bind/net_bind.h"
#include "runtime/vm/vm.h"

using namespace engine;

namespace {

struct NetBindFixture {
	ScriptVM vm;

	NetBindFixture() {
		script::ExportNet(vm);
	}

	~NetBindFixture() {
		script::ShutdownNetBindings();
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_net_bind", nullptr, &result);
	}
};

}  // namespace

TEST_CASE("net module exports all transport libraries", "[net_bind][module]") {
	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
return type(net) .. ',' ..
       type(net.client) .. ',' ..
       type(net.server) .. ',' ..
       type(net.http) .. ',' ..
       type(net.udp_client) .. ',' ..
       type(net.udp_server) .. ',' ..
       type(net.kcp_client) .. ',' ..
       type(net.kcp_server)
)lua",
		result));
	REQUIRE(result == "table,table,table,table,table,table,table,table");
}

TEST_CASE("net.http exports get and post functions", "[net_bind][http]") {
	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("return type(net.http.get) .. ',' .. type(net.http.post)", result));
	REQUIRE(result == "function,function");
}

TEST_CASE("net.http validates callback argument types", "[net_bind][http]") {
	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local ok_get, err_get = pcall(net.http.get, 'http://127.0.0.1', 'not a function')
local ok_post, err_post = pcall(net.http.post, 'http://127.0.0.1', 'body', 'not a function')
return tostring(ok_get) .. ',' ..
       tostring(type(err_get) == 'string' and err_get:find('function') ~= nil) .. ',' ..
       tostring(ok_post) .. ',' ..
       tostring(type(err_post) == 'string' and err_post:find('function') ~= nil)
)lua",
		result));
	REQUIRE(result == "false,true,false,true");
}

TEST_CASE("net.http reports missing EventLoop before starting a request", "[net_bind][http]") {
	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local called = false
local ok, err = pcall(net.http.get, 'http://127.0.0.1', function()
    called = true
end)
return tostring(ok) .. ',' ..
       tostring(called) .. ',' ..
       tostring(type(err) == 'string' and err:find('EventLoop not available') ~= nil)
)lua",
		result));
	REQUIRE(result == "false,false,true");
}

TEST_CASE("net.http rejects oversized POST bodies before creating requests", "[net_bind][http]") {
	REQUIRE(ConfigManager::Instance().LoadServerFromString(R"({
        "http": { "timeout_sec": 1.0 },
        "msgpack": { "max_nesting_depth": 16, "max_payload_size": 1048576 },
        "resource_limits": {
            "max_message_size": 64,
            "max_buffer_capacity": 64,
            "max_http_body_size": 8,
            "max_msgpack_depth": 16
        }
    })"));

	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local ok, err = pcall(net.http.post, 'http://127.0.0.1', string.rep('x', 9), function() end)
return tostring(ok) .. ',' ..
       tostring(type(err) == 'string' and err:find('HTTP body size') ~= nil)
)lua",
		result));
	REQUIRE(result == "false,true");
}

TEST_CASE("net.udp_client.do_request enforces configured message size", "[net_bind][udp]") {
	REQUIRE(ConfigManager::Instance().LoadServerFromString(R"({
        "http": { "timeout_sec": 1.0 },
        "msgpack": { "max_nesting_depth": 16, "max_payload_size": 1048576 },
        "resource_limits": {
            "max_message_size": 8,
            "max_buffer_capacity": 8,
            "max_http_body_size": 1024,
            "max_msgpack_depth": 16
        }
    })"));

	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local ok, err = pcall(net.udp_client.do_request, '127.0.0.1', 9, string.rep('x', 9), 1)
return tostring(ok) .. ',' ..
       tostring(type(err) == 'string' and err:find('message size') ~= nil)
)lua",
		result));
	REQUIRE(result == "false,true");
}

TEST_CASE("net.kcp_client validates tuning argument ranges", "[net_bind][kcp]") {
	NetBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local c = net.kcp_client.new()
local ok_nodelay, err_nodelay = pcall(function()
    return c:set_kcp_nodelay(2, 10, 0, 0)
end)
local ok_window, err_window = pcall(function()
    return c:set_kcp_wnd_size(0, 32)
end)
local ok_mtu, err_mtu = pcall(function()
    return c:set_kcp_mtu(0)
end)
c:close()

return table.concat({
    tostring(ok_nodelay),
    tostring(type(err_nodelay) == 'string' and err_nodelay:find('nodelay') ~= nil),
    tostring(ok_window),
    tostring(type(err_window) == 'string' and err_window:find('sndwnd') ~= nil),
    tostring(ok_mtu),
    tostring(type(err_mtu) == 'string' and err_mtu:find('mtu') ~= nil),
}, ',')
)lua",
		result));
	REQUIRE(result == "false,true,false,true,false,true");
}

TEST_CASE("ShutdownNetBindings is idempotent without live transports", "[net_bind][shutdown]") {
	NetBindFixture f;
	script::ShutdownNetBindings();
	script::ShutdownNetBindings();
	SUCCEED();
}
