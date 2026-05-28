#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "log_init.h"
#include "runtime/rpc/rpc_protocol.h"
#include "runtime/rpc/rpc_client.h"
#include "runtime/rpc/rpc_server.h"

using namespace engine::rpc;

// ═══════════════════════════════════════════════════════════════════════════
// RPC Protocol: message types and struct defaults
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcMessageType enum values", "[rpc][protocol]") {
	REQUIRE(static_cast<uint8_t>(RpcMessageType::kRequest) == 0);
	REQUIRE(static_cast<uint8_t>(RpcMessageType::kResponse) == 1);
	REQUIRE(static_cast<uint8_t>(RpcMessageType::kError) == 2);
}

TEST_CASE("RpcHeader default initialization", "[rpc][protocol]") {
	RpcHeader h;
	REQUIRE(h.msgid == 0);
	REQUIRE(h.service.empty());
	REQUIRE(h.method.empty());
	REQUIRE(h.type == RpcMessageType::kRequest);
}

TEST_CASE("RpcHeader field assignment", "[rpc][protocol]") {
	RpcHeader h;
	h.msgid = 42;
	h.service = "TestService";
	h.method = "TestMethod";
	h.type = RpcMessageType::kResponse;

	REQUIRE(h.msgid == 42);
	REQUIRE(h.service == "TestService");
	REQUIRE(h.method == "TestMethod");
	REQUIRE(h.type == RpcMessageType::kResponse);
}

TEST_CASE("RpcRequest default initialization", "[rpc][protocol]") {
	RpcRequest req;
	REQUIRE(req.header.msgid == 0);
	REQUIRE(req.header.type == RpcMessageType::kRequest);
	REQUIRE(req.body.empty());
}

TEST_CASE("RpcRequest field assignment", "[rpc][protocol]") {
	RpcRequest req;
	req.header.msgid = 100;
	req.header.service = "svc";
	req.header.method = "fn";
	req.body = R"({"key":"value"})";

	REQUIRE(req.header.msgid == 100);
	REQUIRE(req.header.service == "svc");
	REQUIRE(req.header.method == "fn");
	REQUIRE(req.body == R"({"key":"value"})");
}

TEST_CASE("RpcResponse default initialization", "[rpc][protocol]") {
	RpcResponse resp;
	REQUIRE(resp.msgid == 0);
	REQUIRE(resp.success == true);
	REQUIRE(resp.error_code == 0);
	REQUIRE(resp.error_message.empty());
	REQUIRE(resp.body.empty());
}

TEST_CASE("RpcResponse field assignment - success", "[rpc][protocol]") {
	RpcResponse resp;
	resp.msgid = 7;
	resp.success = true;
	resp.body = R"({"result":"ok"})";

	REQUIRE(resp.msgid == 7);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == R"({"result":"ok"})");
}

TEST_CASE("RpcResponse field assignment - error", "[rpc][protocol]") {
	RpcResponse resp;
	resp.msgid = 7;
	resp.success = false;
	resp.error_code = 500;
	resp.error_message = "Internal Error";

	REQUIRE(resp.msgid == 7);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 500);
	REQUIRE(resp.error_message == "Internal Error");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: service registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer has no services by default", "[rpc][server]") {
	RpcServer server;
	REQUIRE_FALSE(server.HasService("nonexistent"));
}

TEST_CASE("RpcServer registers and checks a service", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("MathService",
		[](const std::string& method, const std::string& body) -> std::string {
			return "ok";
		});
	REQUIRE(server.HasService("MathService"));
	REQUIRE_FALSE(server.HasService("OtherService"));
}

TEST_CASE("RpcServer unregisters a service", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("TempService",
		[](const std::string&, const std::string&) -> std::string {
			return "";
		});
	REQUIRE(server.HasService("TempService"));

	server.UnregisterService("TempService");
	REQUIRE_FALSE(server.HasService("TempService"));
}

TEST_CASE("RpcServer unregister nonexistent service is safe", "[rpc][server]") {
	RpcServer server;
	REQUIRE_NOTHROW(server.UnregisterService("does_not_exist"));
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: request handling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer HandleRequest returns error for unknown service", "[rpc][server]") {
	RpcServer server;
	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "UnknownService";
	req.header.method = "DoSomething";
	req.body = R"({"a":1})";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 1);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 404);
	REQUIRE(resp.error_message.find("service not found") != std::string::npos);
	REQUIRE(resp.error_message.find("UnknownService") != std::string::npos);
}

TEST_CASE("RpcServer HandleRequest dispatches to service handler", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("EchoService",
		[](const std::string& method, const std::string& body) -> std::string {
			return "[" + method + "] " + body;
		});

	RpcRequest req;
	req.header.msgid = 10;
	req.header.service = "EchoService";
	req.header.method = "Echo";
	req.body = R"("hello world")";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 10);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "[Echo] \"hello world\"");
}

TEST_CASE("RpcServer HandleRequest with method-specific handler", "[rpc][server]") {
	RpcServer server;

	// Method-specific handler takes priority.
	server.RegisterMethod("MathService", "add",
		[](const std::string& args) -> std::string {
			return R"({"sum":3})";
		});

	// Fallback service handler.
	server.RegisterService("MathService",
		[](const std::string& method, const std::string& body) -> std::string {
			return "fallback:" + method;
		});

	RpcRequest req;
	req.header.msgid = 20;
	req.header.service = "MathService";
	req.header.method = "add";
	req.body = R"({"a":1,"b":2})";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 20);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == R"({"sum":3})");
}

TEST_CASE("RpcServer HandleRequest fallback to service handler for unknown method", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("MathService",
		[](const std::string& method, const std::string& body) -> std::string {
			return "handled:" + method;
		});

	RpcRequest req;
	req.header.msgid = 30;
	req.header.service = "MathService";
	req.header.method = "multiply";
	req.body = "{}";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 30);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "handled:multiply");
}

TEST_CASE("RpcServer HandleRequest error when no handler for method", "[rpc][server]") {
	RpcServer server;
	// Register only a method handler (no service handler).
	server.RegisterMethod("TestSvc", "foo",
		[](const std::string&) -> std::string {
			return "foo_result";
		});

	RpcRequest req;
	req.header.msgid = 40;
	req.header.service = "TestSvc";
	req.header.method = "bar";  // unknown method, no service handler fallback
	req.body = "{}";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 40);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 405);
	REQUIRE(resp.error_message.find("method not found") != std::string::npos);
}

TEST_CASE("RpcServer HandleRequest catches exceptions from handler", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("FaultyService",
		[](const std::string&, const std::string&) -> std::string {
			throw std::runtime_error("boom!");
		});

	RpcRequest req;
	req.header.msgid = 50;
	req.header.service = "FaultyService";
	req.header.method = "crash";
	req.body = "{}";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.msgid == 50);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 500);
	REQUIRE(resp.error_message == "boom!");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: multiple service registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer multiple services", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("SvcA",
		[](const std::string& m, const std::string&) -> std::string {
			return "A:" + m;
		});
	server.RegisterService("SvcB",
		[](const std::string& m, const std::string&) -> std::string {
			return "B:" + m;
		});

	REQUIRE(server.HasService("SvcA"));
	REQUIRE(server.HasService("SvcB"));

	RpcRequest reqA;
	reqA.header.msgid = 1;
	reqA.header.service = "SvcA";
	reqA.header.method = "hello";
	reqA.body = "{}";

	RpcResponse respA = server.HandleRequest(reqA);
	REQUIRE(respA.success == true);
	REQUIRE(respA.body == "A:hello");

	RpcRequest reqB;
	reqB.header.msgid = 2;
	reqB.header.service = "SvcB";
	reqB.header.method = "world";
	reqB.body = "{}";

	RpcResponse respB = server.HandleRequest(reqB);
	REQUIRE(respB.success == true);
	REQUIRE(respB.body == "B:world");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: basic operations
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient default construction", "[rpc][client]") {
	RpcClient client;
	// Construction and destruction are safe.
}

TEST_CASE("RpcClient Call returns a valid future", "[rpc][client]") {
	RpcClient client;
	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");
	REQUIRE(future.valid());
	// No response sent; future should time out.
	auto status = future.wait_for(std::chrono::milliseconds(10));
	REQUIRE(status == std::future_status::timeout);
}

TEST_CASE("RpcClient OnResponse resolves a pending future", "[rpc][client]") {
	RpcClient client;

	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");
	REQUIRE(future.valid());

	// Should not be ready yet.
	auto status = future.wait_for(std::chrono::milliseconds(5));
	REQUIRE(status == std::future_status::timeout);

	// Simulate a response arriving.
	RpcResponse resp;
	resp.msgid = 1;  // First message ID.
	resp.success = true;
	resp.body = R"({"result":42})";
	client.OnResponse(resp);

	// Now the future should be ready.
	status = future.wait_for(std::chrono::milliseconds(100));
	REQUIRE(status == std::future_status::ready);

	RpcResponse result = future.get();
	REQUIRE(result.success == true);
	REQUIRE(result.body == R"({"result":42})");
}

TEST_CASE("RpcClient CallSync returns timeout when no response", "[rpc][client]") {
	RpcClient client;
	auto resp = client.CallSync("TestService", "TestMethod", R"({"x":1})", 10);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_message == "timeout");
}

TEST_CASE("RpcClient CallSync returns result on response", "[rpc][client]") {
	RpcClient client;

	// Start a sync call in another thread so we can inject a response.
	std::atomic<bool> call_started{false};
	std::thread t([&]() {
		call_started = true;
		// Long timeout so we have time to inject.
		auto resp = client.CallSync("TestService", "TestMethod", R"({"x":1})", 5000);
		REQUIRE(resp.success == true);
		REQUIRE(resp.body == R"({"result":"sync_ok"})");
	});

	// Wait until the call has been made and the future is waiting.
	while (!call_started) {
		std::this_thread::yield();
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	// Inject the response while the sync call waits.
	RpcResponse resp;
	resp.msgid = 1;
	resp.success = true;
	resp.body = R"({"result":"sync_ok"})";
	client.OnResponse(resp);

	t.join();
}

TEST_CASE("RpcClient CallAsync invokes callback on response", "[rpc][client]") {
	RpcClient client;

	bool callback_called = false;
	RpcResponse captured;

	client.CallAsync("TestService", "TestMethod", R"({"x":1})",
		[&](const RpcResponse& r) {
			callback_called = true;
			captured = r;
		});

	REQUIRE_FALSE(callback_called);

	RpcResponse resp;
	resp.msgid = 1;
	resp.success = true;
	resp.body = R"({"value":99})";
	client.OnResponse(resp);

	REQUIRE(callback_called);
	REQUIRE(captured.success == true);
	REQUIRE(captured.body == R"({"value":99})");
}

TEST_CASE("RpcClient OnResponse with unknown msgid is safe", "[rpc][client]") {
	RpcClient client;
	RpcResponse resp;
	resp.msgid = 999;
	resp.success = true;
	REQUIRE_NOTHROW(client.OnResponse(resp));
}

TEST_CASE("RpcClient ProcessTimeouts is safe to call", "[rpc][client]") {
	RpcClient client;
	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");
	REQUIRE(future.valid());
	REQUIRE_NOTHROW(client.ProcessTimeouts());
}

TEST_CASE("RpcClient message IDs are sequential", "[rpc][client]") {
	RpcClient client;

	auto f1 = client.Call("Svc", "m1", "{}");
	auto f2 = client.Call("Svc", "m2", "{}");
	auto f3 = client.Call("Svc", "m3", "{}");

	REQUIRE(f1.valid());
	REQUIRE(f2.valid());
	REQUIRE(f3.valid());

	// Resolve out of order - msgid 3 first.
	RpcResponse r3;
	r3.msgid = 3;
	r3.success = true;
	r3.body = "third";
	client.OnResponse(r3);

	auto status = f3.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);
	REQUIRE(f3.get().body == "third");

	// Resolve msgid 1.
	RpcResponse r1;
	r1.msgid = 1;
	r1.success = true;
	r1.body = "first";
	client.OnResponse(r1);

	status = f1.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);
	REQUIRE(f1.get().body == "first");

	// Resolve msgid 2.
	RpcResponse r2;
	r2.msgid = 2;
	r2.success = true;
	r2.body = "second";
	client.OnResponse(r2);

	status = f2.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);
	REQUIRE(f2.get().body == "second");
}

TEST_CASE("RpcClient handles error response", "[rpc][client]") {
	RpcClient client;

	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");

	RpcResponse resp;
	resp.msgid = 1;
	resp.success = false;
	resp.error_code = 403;
	resp.error_message = "forbidden";
	client.OnResponse(resp);

	auto status = future.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);

	RpcResponse result = future.get();
	REQUIRE(result.success == false);
	REQUIRE(result.error_code == 403);
	REQUIRE(result.error_message == "forbidden");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC integration: client sends, server responds, client receives
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RPC client-server round-trip", "[rpc][integration]") {
	RpcServer server;
	server.RegisterService("CalcService",
		[](const std::string& method, const std::string& body) -> std::string {
			if (method == "add") return R"({"result":30})";
			if (method == "sub") return R"({"result":10})";
			throw std::runtime_error("unknown method: " + method);
		});

	RpcClient client;

	// Client makes a call; we manually route the request through the server
	// and feed the response back to the client.
	auto future = client.Call("CalcService", "add", R"({"a":10,"b":20})");

	// Simulate: the client's transport would build a request and send it.
	// We construct the request manually and pass it to the server.
	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "CalcService";
	req.header.method = "add";
	req.header.type = RpcMessageType::kRequest;
	req.body = R"({"a":10,"b":20})";

	RpcResponse svcResp = server.HandleRequest(req);
	REQUIRE(svcResp.success == true);
	REQUIRE(svcResp.body == R"({"result":30})");

	// Feed the server's response back to the client.
	client.OnResponse(svcResp);

	auto status = future.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);

	RpcResponse result = future.get();
	REQUIRE(result.success == true);
	REQUIRE(result.body == R"({"result":30})");
}
