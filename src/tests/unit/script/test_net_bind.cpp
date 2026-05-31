#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log_init.h"
#include "runtime/script/net_bind.h"
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

TEST_CASE("ShutdownNetBindings is idempotent without live transports", "[net_bind][shutdown]") {
	NetBindFixture f;
	script::ShutdownNetBindings();
	script::ShutdownNetBindings();
	SUCCEED();
}
