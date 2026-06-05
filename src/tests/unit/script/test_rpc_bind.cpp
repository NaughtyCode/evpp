#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "log_init.h"
#include "runtime/rpc/rpc_client.h"
#include "runtime/rpc/rpc_protocol.h"
#include "runtime/rpc/rpc_server.h"
#include "runtime/rpc/bind/rpc_bind.h"
#include "runtime/vm/vm.h"

using namespace engine;
using namespace engine::rpc;

struct RpcBindFixture {
	ScriptVM vm;

	RpcBindFixture() {
		script::ExportRpc(vm);
	}

	bool RunLua(const std::string& code, std::string* err = nullptr) {
		return vm.DoString(code, "test_rpc_bind", err);
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_rpc_bind", nullptr, &result);
	}
};

// Helper: get RpcServer pointer from a Lua server table at the top of stack.
static rpc::RpcServer* GetServer(lua_State* L) {
	return script::RpcBind_GetServer(L, -1);
}

// Simulate a network request: call HandleRequest on another thread,
// then drain the deferred queue on the main thread.
static RpcResponse SimulateRequest(RpcBindFixture& f, rpc::RpcServer* server,
                                   const std::string& service,
                                   const std::string& method,
                                   const std::string& body = "{}") {
	RpcResponse response;
	std::thread net_thread([&]() {
		RpcRequest req;
		req.header.msgid = 1;
		req.header.service = service;
		req.header.method = method;
		req.body = body;
		response = server->HandleRequest(req);
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	script::UpdateRpcBindings(f.vm);
	net_thread.join();
	return response;
}

// ═══════════════════════════════════════════════════════════════════════════
// Module export
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("rpc module is exported", "[rpc_bind][module]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("return type(rpc)", result));
	REQUIRE(result == "table");
}

TEST_CASE("rpc.new_server and rpc.new_client exist", "[rpc_bind][module]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("return type(rpc.new_server) .. ',' .. type(rpc.new_client)", result));
	REQUIRE(result == "function,function");
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: creation & lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("server table has _ctx lightuserdata", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("s = rpc.new_server(); return type(s._ctx)", result));
	REQUIRE(result == "userdata");
}

TEST_CASE("server has all expected methods", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"return type(s.register_service) .. ',' .. type(s.unregister_service) .. ',' .. type(s.stop)",
		result));
	REQUIRE(result == "function,function,function");
}

TEST_CASE("server:register_service returns true", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"return tostring(s:register_service('test', function(svc, m, b) return '{}' end))",
		result));
	REQUIRE(result == "true");
}

TEST_CASE("server:stop returns true, then false on double-stop", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"s:stop()\n"
		"return tostring(s:stop())", result));
	REQUIRE(result == "false");
}

TEST_CASE("server:register_service after stop returns error", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"s:stop()\n"
		"local ok, err = s:register_service('x', function() end)\n"
		"return tostring(ok) .. ',' .. tostring(err)", result));
	REQUIRE(result.find("nil") != std::string::npos);
	REQUIRE(result.find("closed") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: unregistration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("server:unregister_service removes a service", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"s:register_service('test', function(svc, m, b) return '{}' end)\n"
		"return tostring(s:unregister_service('test'))", result));
	REQUIRE(result == "true");
}

TEST_CASE("register_service replaces previous callback cleanly", "[rpc_bind][server]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"s = rpc.new_server()\n"
		"s:register_service('test', function(svc, m, b) return 'first' end)\n"
		"s:register_service('test', function(svc, m, b) return 'second' end)\n"
		"return 'ok'", result));
	REQUIRE(result == "ok");
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: deferred dispatch (HandleRequest → queue → UpdateRpcBindings)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("deferred dispatch routes request to Lua callback", "[rpc_bind][dispatch]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('EchoSvc', function(svc, method, body)\n"
		"  return '[' .. svc .. ':' .. method .. '] ' .. body\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	auto resp = SimulateRequest(f, server, "EchoSvc", "hello", "world");
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "[EchoSvc:hello] world");
}

TEST_CASE("Lua callback receives all three args (service, method, body)", "[rpc_bind][dispatch]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"captured = {}\n"
		"s:register_service('ArgsTest', function(svc, method, body)\n"
		"  captured.svc = svc\n"
		"  captured.method = method\n"
		"  captured.body = body\n"
		"  return '{}'\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	auto resp = SimulateRequest(f, server, "ArgsTest", "TestMethod", R"({"key":"v"})");
	REQUIRE(resp.success == true);

	std::string svc, method, body;
	REQUIRE(f.RunLuaResult("return captured.svc", svc));
	REQUIRE(svc == "ArgsTest");
	REQUIRE(f.RunLuaResult("return captured.method", method));
	REQUIRE(method == "TestMethod");
	REQUIRE(f.RunLuaResult("return captured.body", body));
	REQUIRE(body == R"({"key":"v"})");
}

TEST_CASE("deferred dispatch handles multiple services", "[rpc_bind][dispatch]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('SvcA', function(svc, m, b) return 'A:'..m end)\n"
		"s:register_service('SvcB', function(svc, m, b) return 'B:'..m end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	RpcResponse respA, respB;
	std::thread tA([&]() {
		RpcRequest req;
		req.header.msgid = 1; req.header.service = "SvcA"; req.header.method = "hello";
		respA = server->HandleRequest(req);
	});
	std::thread tB([&]() {
		RpcRequest req;
		req.header.msgid = 2; req.header.service = "SvcB"; req.header.method = "world";
		respB = server->HandleRequest(req);
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	script::UpdateRpcBindings(f.vm);
	tA.join();
	tB.join();

	REQUIRE(respA.success == true);
	REQUIRE(respA.body == "A:hello");
	REQUIRE(respB.success == true);
	REQUIRE(respB.body == "B:world");
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: error handling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Lua callback error is propagated as JSON error body", "[rpc_bind][error]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('BadSvc', function(svc, m, b)\n"
		"  error('intentional test error')\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	auto resp = SimulateRequest(f, server, "BadSvc", "crash");
	// HandleRequest itself succeeds; the Lua error is inside the JSON body.
	REQUIRE(resp.success == true);
	REQUIRE(resp.body.find("\"error\"") != std::string::npos);
	REQUIRE(resp.body.find("intentional test error") != std::string::npos);
}

TEST_CASE("Lua callback error body escapes JSON string content", "[rpc_bind][error]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('BadJsonSvc', function(svc, m, b)\n"
		"  error('bad \"quote\"')\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	auto resp = SimulateRequest(f, server, "BadJsonSvc", "crash");
	REQUIRE(resp.success == true);
	REQUIRE(resp.body.find("\\\"quote\\\"") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: stop rejects in-flight handler
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("server:stop rejects pending handler", "[rpc_bind][stop]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('SlowSvc', function(svc, m, b)\n"
		"  return 'slow_response'\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	RpcResponse response;
	std::atomic<bool> call_started{false};
	std::thread net_thread([&]() {
		call_started = true;
		RpcRequest req;
		req.header.msgid = 1; req.header.service = "SlowSvc"; req.header.method = "wait";
		response = server->HandleRequest(req);
	});

	while (!call_started) { std::this_thread::yield(); }
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	REQUIRE(f.RunLua("s:stop()\n"));
	net_thread.join();

	REQUIRE(response.body.find("\"error\"") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: creation & basic operations
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client table has call and stop methods", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"return type(c.call) .. ',' .. type(c.stop)", result));
	REQUIRE(result == "function,function");
}

TEST_CASE("client:call returns nil+transport error when no transport", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"local resp, err = c:call('svc', 'method', '{}', 10)\n"
		"return tostring(resp) .. ',' .. tostring(err)", result));
	REQUIRE(result.find("nil") != std::string::npos);
	REQUIRE(result.find("no transport") != std::string::npos);
}

TEST_CASE("client:stop returns true", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("c = rpc.new_client(); return tostring(c:stop())", result));
	REQUIRE(result == "true");
}

TEST_CASE("client:call after stop returns nil+closed error", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"c:stop()\n"
		"local resp, err = c:call('svc', 'm', '{}', 10)\n"
		"return tostring(resp) .. ',' .. tostring(err)", result));
	REQUIRE(result.find("nil") != std::string::npos);
	REQUIRE(result.find("closed") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Shutdown
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShutdownRpcBindings does not crash with active instances", "[rpc_bind][shutdown]") {
	RpcBindFixture f;
	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('test', function(svc, m, b) return '{}' end)\n"
		"c = rpc.new_client()\n"));
	REQUIRE_NOTHROW(script::ShutdownRpcBindings(f.vm));
}

TEST_CASE("ExportRpc is idempotent after ShutdownRpcBindings", "[rpc_bind][shutdown]") {
	RpcBindFixture f;
	REQUIRE(f.RunLua("s = rpc.new_server()\n"));
	script::ShutdownRpcBindings(f.vm);
	script::ExportRpc(f.vm);

	std::string result;
	REQUIRE(f.RunLuaResult("return type(rpc.new_server())", result));
	REQUIRE(result == "table");
}

// ═══════════════════════════════════════════════════════════════════════════
// Multiple instances
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("multiple servers are independent", "[rpc_bind][multi]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s1 = rpc.new_server()\n"
		"s2 = rpc.new_server()\n"
		"s1:register_service('A', function(svc, m, b) return 's1:'..m end)\n"
		"s2:register_service('B', function(svc, m, b) return 's2:'..m end)\n"));

	auto* L = f.vm.GetState();

	lua_getglobal(L, "s1");
	auto* s1 = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(s1 != nullptr);

	lua_getglobal(L, "s2");
	auto* s2 = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(s2 != nullptr);

	REQUIRE(s1 != s2);

	RpcResponse r1, r2;
	std::thread t1([&]() {
		RpcRequest req; req.header.service = "A"; req.header.method = "hello";
		r1 = s1->HandleRequest(req);
	});
	std::thread t2([&]() {
		RpcRequest req; req.header.service = "B"; req.header.method = "world";
		r2 = s2->HandleRequest(req);
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	script::UpdateRpcBindings(f.vm);
	t1.join(); t2.join();

	REQUIRE(r1.success == true);
	REQUIRE(r1.body == "s1:hello");
	REQUIRE(r2.success == true);
	REQUIRE(r2.body == "s2:world");
}

TEST_CASE("stopping one server does not affect the other", "[rpc_bind][multi]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s1 = rpc.new_server()\n"
		"s2 = rpc.new_server()\n"
		"s1:register_service('A', function(svc, m, b) return 'ok' end)\n"
		"s2:register_service('B', function(svc, m, b) return 'ok' end)\n"
		"s1:stop()\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s2");
	auto* s2 = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(s2 != nullptr);

	auto resp = SimulateRequest(f, s2, "B", "test");
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "ok");
}


// ═══════════════════════════════════════════════════════════════════════════
// Client: set_send_callback re-registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client set_send_callback re-registration replaces old", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"count1 = 0; count2 = 0\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) count1 = count1 + 1 end)\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) count2 = count2 + 1 end)\n"
		"c:call('Svc', 'm', '{}', 10)\n"  // triggers send callback
		"return count1 .. ',' .. count2", result));
	REQUIRE(result.find("0,1") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: call with send_callback set
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client call with send_callback succeeds when response injected", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"sent = {}\n"
		"c:set_send_callback(function(msgid, svc, mtd, body)\n"
		"  sent.msgid = msgid\n"
		"  sent.svc = svc\n"
		"  sent.mtd = mtd\n"
		"  sent.body = body\n"
		"end)\n"
		"c:call('RemoteSvc', 'RemoteMethod', '{\"a\":1}', 10)\n"
		"return sent.svc .. ',' .. sent.mtd .. ',' .. sent.body", result));
	REQUIRE(result.find("RemoteSvc,RemoteMethod") != std::string::npos);
	REQUIRE(result.find("{\"a\":1}") != std::string::npos);
}

TEST_CASE("client call returns send callback errors without waiting for timeout", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body)\n"
		"  error('send failed \"closed\"')\n"
		"end)\n"
		"local body, err = c:call('RemoteSvc', 'RemoteMethod', '{}', 1000)\n"
		"return tostring(body) .. ',' .. tostring(err)", result));
	REQUIRE(result.find("nil") != std::string::npos);
	REQUIRE(result.find("transport send failed") != std::string::npos);
	REQUIRE(result.find("closed") != std::string::npos);
}

TEST_CASE("client call preserves binary response body", "[rpc_bind][client]") {
	RpcBindFixture f;
	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	std::thread responder([client]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		client->OnResponse(RpcResponse::Ok(1, std::string("a\0b", 3)));
	});

	std::string result;
	bool ok = f.RunLuaResult(
		"local body, err = c:call('RemoteSvc', 'RemoteMethod', '{}', 1000)\n"
		"if err then return 'err:' .. err end\n"
		"return body", result);
	responder.join();

	REQUIRE(ok);
	REQUIRE(result == std::string("a\0b", 3));
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: call_async deferred response (new deferred queue mechanism)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client call_async invokes callback via UpdateRpcBindings", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"async_result = nil\n"
		"async_err = nil\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  async_result = body\n"
		"  async_err = err\n"
		"end)\n"));

	// The call_async callback hasn't fired yet — deferred queue.
	std::string result;
	REQUIRE(f.RunLuaResult("return tostring(async_result)", result));
	REQUIRE(result == "nil");

	// Inject a response into the C++ client and drain via UpdateRpcBindings.
	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	RpcResponse resp;
	resp.msgid = 1;
	resp.success = true;
	resp.body = R"({"ok":true})";
	client->OnResponse(resp);

	// Drain deferred responses on the main thread.
	script::UpdateRpcBindings(f.vm);

	REQUIRE(f.RunLuaResult("return tostring(async_result)", result));
	REQUIRE(result == R"({"ok":true})");
}

TEST_CASE("client call_async with error response", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"async_body = 'not_set'\n"
		"async_err = 'not_set'\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  async_body = body\n"
		"  async_err = err\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	RpcResponse resp;
	resp.msgid = 1;
	resp.success = false;
	resp.error_code = 500;
	resp.error_message = "server error";
	client->OnResponse(resp);

	script::UpdateRpcBindings(f.vm);

	std::string body, err;
	REQUIRE(f.RunLuaResult("return tostring(async_body)", body));
	REQUIRE(body == "nil");
	REQUIRE(f.RunLuaResult("return tostring(async_err)", err));
	REQUIRE(err == "server error");
}

TEST_CASE("client call_async with no transport returns error", "[rpc_bind][client]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"c = rpc.new_client()\n"
		"async_body = 'init'\n"
		"async_err = 'init'\n"
		"local ok, err = c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  async_body = tostring(body)\n"
		"  async_err = tostring(err)\n"
		"end)\n"
		"return tostring(ok) .. ',' .. tostring(err) .. ',' .. async_err", result));
	REQUIRE(result.find("nil") != std::string::npos);
	REQUIRE(result.find("no transport") != std::string::npos);
	REQUIRE(result.find("init") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: call_async after stop
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client stop drains call_async queue without invoking Lua", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"async_called = false\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  async_called = true\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	// Don't inject a response; stop the client instead.
	// RpcClient destructor fulfills with "client destroyed",
	// which pushes into deferred_responses. stop drains it.
	REQUIRE(f.RunLua("c:stop()\n"));

	// callback should NOT have been called (drained without invocation).
	std::string result;
	REQUIRE(f.RunLuaResult("return tostring(async_called)", result));
	REQUIRE(result == "false");
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: multiple call_async interleaved
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client multiple call_async interleaved", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"results = {}\n"
		"c:call_async('Svc', 'first', '{}', function(body, err)\n"
		"  results[1] = body\n"
		"end)\n"
		"c:call_async('Svc', 'second', '{}', function(body, err)\n"
		"  results[2] = body\n"
		"end)\n"
		"c:call_async('Svc', 'third', '{}', function(body, err)\n"
		"  results[3] = body\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	// Resolve in reverse order.
	client->OnResponse(RpcResponse::Ok(3, "third"));
	client->OnResponse(RpcResponse::Ok(2, "second"));
	client->OnResponse(RpcResponse::Ok(1, "first"));

	script::UpdateRpcBindings(f.vm);

	std::string r1, r2, r3;
	REQUIRE(f.RunLuaResult("return results[1]", r1));
	REQUIRE(r1 == "first");
	REQUIRE(f.RunLuaResult("return results[2]", r2));
	REQUIRE(r2 == "second");
	REQUIRE(f.RunLuaResult("return results[3]", r3));
	REQUIRE(r3 == "third");
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: call_async callback Lua error handling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client call_async Lua callback error does not crash", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  error('intentional callback error')\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	client->OnResponse(RpcResponse::Ok(1, "data"));
	int top = lua_gettop(L);
	REQUIRE_NOTHROW(script::UpdateRpcBindings(f.vm));
	REQUIRE(lua_gettop(L) == top);
}

TEST_CASE("client call_async callback may stop client during update", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"callback_seen = false\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  callback_seen = true\n"
		"  c:stop()\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "c");
	auto* client = script::RpcBind_GetClient(L, -1);
	lua_pop(L, 1);
	REQUIRE(client != nullptr);

	client->OnResponse(RpcResponse::Ok(1, "data"));
	int top = lua_gettop(L);
	REQUIRE_NOTHROW(script::UpdateRpcBindings(f.vm));
	REQUIRE(lua_gettop(L) == top);

	std::string result;
	REQUIRE(f.RunLuaResult("return tostring(callback_seen)", result));
	REQUIRE(result == "true");
}

TEST_CASE("server service callback may stop server during dispatch", "[rpc_bind][server]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('StopSvc', function(svc, m, b)\n"
		"  s:stop()\n"
		"  return 'stopped'\n"
		"end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	int top = lua_gettop(L);
	auto resp = SimulateRequest(f, server, "StopSvc", "Now");
	REQUIRE(lua_gettop(L) == top);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "stopped");
}

// ═══════════════════════════════════════════════════════════════════════════
// Client: call_async timeout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("client call_async timeout invokes callback with error", "[rpc_bind][client]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"c = rpc.new_client()\n"
		"c:set_send_callback(function(msgid, svc, mtd, body) end)\n"
		"timeout_body = 'init'\n"
		"timeout_err = 'init'\n"
		"c:call_async('Svc', 'Method', '{}', function(body, err)\n"
		"  timeout_body = tostring(body)\n"
		"  timeout_err = tostring(err)\n"
		"end, 1)\n"));

	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	REQUIRE_NOTHROW(script::UpdateRpcBindings(f.vm));

	std::string body, err;
	REQUIRE(f.RunLuaResult("return timeout_body", body));
	REQUIRE(body == "nil");
	REQUIRE(f.RunLuaResult("return timeout_err", err));
	REQUIRE(err == "timeout");
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: service re-registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("server register_service re-registration changes behavior", "[rpc_bind][server]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('ReSvc', function(svc, m, b) return 'first' end)\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);

	auto resp1 = SimulateRequest(f, server, "ReSvc", "test");
	REQUIRE(resp1.body == "first");

	// Re-register with new behavior.
	REQUIRE(f.RunLua(
		"s:register_service('ReSvc', function(svc, m, b) return 'second' end)\n"));

	auto resp2 = SimulateRequest(f, server, "ReSvc", "test");
	REQUIRE(resp2.body == "second");
}

// ═══════════════════════════════════════════════════════════════════════════
// Stress: multiple servers and clients
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("create and stop many servers and clients", "[rpc_bind][stress]") {
	RpcBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"for i = 1, 10 do\n"
		"  local s = rpc.new_server()\n"
		"  s:register_service('Svc'..i, function(svc, m, b) return 'ok' end)\n"
		"  s:stop()\n"
		"end\n"
		"return 'done'", result));
	REQUIRE(result == "done");

	REQUIRE(f.RunLuaResult(
		"for i = 1, 10 do\n"
		"  local c = rpc.new_client()\n"
		"  c:stop()\n"
		"end\n"
		"return 'done'", result));
	REQUIRE(result == "done");
}

// ═══════════════════════════════════════════════════════════════════════════
// Server: unregister then HandleRequest
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("server unregister then HandleRequest returns 404", "[rpc_bind][server]") {
	RpcBindFixture f;

	REQUIRE(f.RunLua(
		"s = rpc.new_server()\n"
		"s:register_service('Temp', function(svc, m, b) return 'ok' end)\n"
		"s:unregister_service('Temp')\n"));

	auto* L = f.vm.GetState();
	lua_getglobal(L, "s");
	auto* server = GetServer(L);
	lua_pop(L, 1);
	REQUIRE(server != nullptr);

	auto resp = SimulateRequest(f, server, "Temp", "test");
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 404);
}
