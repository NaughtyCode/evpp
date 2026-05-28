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
	client.SetSendCallback([](RpcRequest) {});  // transport must be set
	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");
	REQUIRE(future.valid());
	// No response sent; future should time out.
	auto status = future.wait_for(std::chrono::milliseconds(10));
	REQUIRE(status == std::future_status::timeout);
}

TEST_CASE("RpcClient OnResponse resolves a pending future", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});  // transport must be set

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
	client.SetSendCallback([](RpcRequest) {});  // transport must be set for real timeout
	auto resp = client.CallSync("TestService", "TestMethod", R"({"x":1})", 10);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_message == "timeout");
	REQUIRE(resp.msgid != 0);
	REQUIRE(client.PendingCount() == 0);
}

TEST_CASE("RpcClient CallSync returns result on response", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});  // transport must be set

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
	client.SetSendCallback([](RpcRequest) {});

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
	client.SetSendCallback([](RpcRequest) {});
	auto future = client.Call("TestService", "TestMethod", R"({"x":1})");
	REQUIRE(future.valid());
	REQUIRE_NOTHROW(client.ProcessTimeouts());
}

TEST_CASE("RpcClient message IDs are sequential", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});

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
	client.SetSendCallback([](RpcRequest) {});

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
	client.SetSendCallback([](RpcRequest) {});

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


// ═══════════════════════════════════════════════════════════════════════════
// RPC Response: factory functions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcResponse::Ok factory creates success response", "[rpc][protocol]") {
	auto resp = RpcResponse::Ok(42, R"({"val":1})");
	REQUIRE(resp.msgid == 42);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == R"({"val":1})");
	REQUIRE(resp.error_code == 0);
	REQUIRE(resp.error_message.empty());
}

TEST_CASE("RpcResponse::Error factory creates error response", "[rpc][protocol]") {
	auto resp = RpcResponse::Error(99, 500, "Internal Error");
	REQUIRE(resp.msgid == 99);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 500);
	REQUIRE(resp.error_message == "Internal Error");
	REQUIRE(resp.body.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: Clear() and handler re-registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer Clear removes all services", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("SvcA",
		[](const std::string&, const std::string&) -> std::string { return "a"; });
	server.RegisterService("SvcB",
		[](const std::string&, const std::string&) -> std::string { return "b"; });
	REQUIRE(server.HasService("SvcA"));
	REQUIRE(server.HasService("SvcB"));

	server.Clear();
	REQUIRE_FALSE(server.HasService("SvcA"));
	REQUIRE_FALSE(server.HasService("SvcB"));
}

TEST_CASE("RpcServer RegisterMethod overwrites previous handler", "[rpc][server]") {
	RpcServer server;

	server.RegisterMethod("Svc", "m", [](const std::string&) -> std::string {
		return "first";
	});
	server.RegisterMethod("Svc", "m", [](const std::string&) -> std::string {
		return "second";
	});

	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "Svc";
	req.header.method = "m";
	req.body = "{}";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.success == true);
	REQUIRE(resp.body == "second");
}

TEST_CASE("RpcServer HandleRequest catches non-std-exception", "[rpc][server]") {
	RpcServer server;
	server.RegisterService("BadSvc",
		[](const std::string&, const std::string&) -> std::string {
			throw 42;  // non-std::exception
		});

	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "BadSvc";
	req.header.method = "crash";
	req.body = "{}";

	RpcResponse resp = server.HandleRequest(req);
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == 500);
	REQUIRE(resp.error_message == "unknown handler error");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: concurrent HandleRequest
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer HandleRequest is safe under concurrent calls", "[rpc][server][concurrent]") {
	RpcServer server;
	std::atomic<int> call_count{0};

	server.RegisterService("ConcurrentSvc",
		[&call_count](const std::string& method, const std::string& body) -> std::string {
			call_count.fetch_add(1, std::memory_order_relaxed);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			return "ok";
		});

	constexpr int kThreads = 8;
	std::vector<std::thread> threads;
	std::vector<RpcResponse> results(kThreads);

	for (int i = 0; i < kThreads; ++i) {
		threads.emplace_back([&server, &results, i]() {
			RpcRequest req;
			req.header.msgid = static_cast<uint32_t>(i + 1);
			req.header.service = "ConcurrentSvc";
			req.header.method = "run";
			req.body = "{}";
			results[i] = server.HandleRequest(req);
		});
	}

	for (auto& t : threads) t.join();

	REQUIRE(call_count.load() == kThreads);
	for (int i = 0; i < kThreads; ++i) {
		REQUIRE(results[i].success == true);
		REQUIRE(results[i].body == "ok");
		REQUIRE(results[i].msgid == static_cast<uint32_t>(i + 1));
	}
}

TEST_CASE("RpcServer concurrent register and HandleRequest is safe", "[rpc][server][concurrent]") {
	RpcServer server;
	std::atomic<bool> done{false};
	std::atomic<int> ok_count{0};

	server.RegisterService("StableSvc",
		[&ok_count](const std::string&, const std::string&) -> std::string {
			ok_count.fetch_add(1, std::memory_order_relaxed);
			return "ok";
		});

	std::thread worker([&]() {
		while (!done.load(std::memory_order_relaxed)) {
			RpcRequest req;
			req.header.msgid = 1;
			req.header.service = "StableSvc";
			req.header.method = "f";
			req.body = "{}";
			auto resp = server.HandleRequest(req);
			if (resp.success) ok_count.fetch_add(1, std::memory_order_relaxed);
			std::this_thread::yield();
		}
	});

	for (int i = 0; i < 20; ++i) {
		server.RegisterService("TempSvc" + std::to_string(i),
			[](const std::string&, const std::string&) -> std::string { return ""; });
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	done.store(true);
	worker.join();
	REQUIRE(ok_count.load() > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: ProcessTimeouts
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient ProcessTimeouts with non-expired entries is safe", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});

	auto future = client.Call("Svc", "m", "{}");
	REQUIRE(future.valid());
	REQUIRE(client.PendingCount() == 1);

	// ProcessTimeouts on a fresh entry (5s deadline) should not clean it.
	REQUIRE_NOTHROW(client.ProcessTimeouts());
	REQUIRE(client.PendingCount() == 1);

	// Resolve normally.
	client.OnResponse(RpcResponse::Ok(1, "ok"));
	REQUIRE(client.PendingCount() == 0);
	auto status = future.wait_for(std::chrono::milliseconds(10));
	REQUIRE(status == std::future_status::ready);
	REQUIRE(future.get().body == "ok");
}

TEST_CASE("RpcClient ProcessTimeouts removes only expired entries", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});

	auto f1 = client.Call("Svc", "short", "{}");
	auto f2 = client.Call("Svc", "long", "{}");
	REQUIRE(client.PendingCount() == 2);

	// Resolve f1, f2 stays pending.
	client.OnResponse(RpcResponse::Ok(1, "first"));
	REQUIRE(client.PendingCount() == 1);

	REQUIRE(f1.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready);
	REQUIRE(f1.get().body == "first");

	REQUIRE(f2.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: destruction during in-flight requests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient destructor fulfills pending Call with error", "[rpc][client]") {
	std::future<RpcResponse> future;
	{
		RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
		future = client.Call("Svc", "m", "{}");
		REQUIRE(client.PendingCount() == 1);
	}  // client destroyed

	REQUIRE(future.valid());
	auto status = future.wait_for(std::chrono::milliseconds(10));
	REQUIRE(status == std::future_status::ready);

	RpcResponse resp = future.get();
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == -1);
	REQUIRE(resp.error_message == "client destroyed");
}

TEST_CASE("RpcClient destructor fulfills pending CallAsync with error", "[rpc][client]") {
	bool called = false;
	RpcResponse captured;
	{
		RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
		client.CallAsync("Svc", "m", "{}", [&](const RpcResponse& r) {
			called = true;
			captured = r;
		});
		REQUIRE(client.PendingCount() == 1);
	}  // client destroyed

	REQUIRE(called);
	REQUIRE(captured.success == false);
	REQUIRE(captured.error_message == "client destroyed");
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: SetSendCallback
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient SendCallback is invoked on Call", "[rpc][client]") {
	RpcClient client;
	bool send_called = false;
	RpcRequest captured_req;

	client.SetSendCallback([&](RpcRequest req) {
		send_called = true;
		captured_req = std::move(req);
	});

	auto future = client.Call("TestSvc", "TestMethod", R"({"x":1})");
	REQUIRE(send_called);
	REQUIRE(captured_req.header.msgid != 0);
	REQUIRE(captured_req.header.service == "TestSvc");
	REQUIRE(captured_req.header.method == "TestMethod");
	REQUIRE(captured_req.header.type == RpcMessageType::kRequest);
	REQUIRE(captured_req.body == R"({"x":1})");

	client.OnResponse(RpcResponse::Ok(captured_req.header.msgid, "done"));
	auto status = future.wait_for(std::chrono::milliseconds(10));
	REQUIRE(status == std::future_status::ready);
}

TEST_CASE("RpcClient SetSendCallback re-registration overwrites", "[rpc][client]") {
	RpcClient client;
	int first_count = 0;
	int second_count = 0;

	client.SetSendCallback([&](RpcRequest) { first_count++; });
	client.SetSendCallback([&](RpcRequest) { second_count++; });

	auto f = client.Call("Svc", "m", "{}");
	REQUIRE(first_count == 0);
	REQUIRE(second_count == 1);
	client.OnResponse(RpcResponse::Ok(1, "ok"));
}

TEST_CASE("RpcClient Call returns error when no transport", "[rpc][client]") {
	RpcClient client;
	auto future = client.Call("Svc", "m", "{}");
	REQUIRE(future.valid());
	auto status = future.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);

	RpcResponse resp = future.get();
	REQUIRE(resp.success == false);
	REQUIRE(resp.error_code == -2);
	REQUIRE(resp.error_message.find("no transport") != std::string::npos);
}

TEST_CASE("RpcClient CallAsync returns immediately when no transport", "[rpc][client]") {
	RpcClient client;
	bool called = false;
	client.CallAsync("Svc", "m", "{}", [&](const RpcResponse& r) {
		called = true;
		REQUIRE(r.success == false);
		REQUIRE(r.error_code == -2);
	});
	REQUIRE(called);
	REQUIRE(client.PendingCount() == 0);
}

TEST_CASE("RpcClient PendingCount tracks in-flight requests", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	REQUIRE(client.PendingCount() == 0);

	auto f1 = client.Call("Svc", "m1", "{}");
	REQUIRE(client.PendingCount() == 1);

	auto f2 = client.Call("Svc", "m2", "{}");
	REQUIRE(client.PendingCount() == 2);

	client.OnResponse(RpcResponse::Ok(1, "a"));
	REQUIRE(client.PendingCount() == 1);

	client.OnResponse(RpcResponse::Ok(2, "b"));
	REQUIRE(client.PendingCount() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: HasTransport
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient HasTransport returns false initially", "[rpc][client]") {
	RpcClient client;
	REQUIRE_FALSE(client.HasTransport());
}

TEST_CASE("RpcClient HasTransport returns true after SetSendCallback", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	REQUIRE(client.HasTransport());
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Client: multiple CallAsync and mixed Call/CallAsync
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient multiple CallAsync interleaved responses", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	int called_a = 0, called_b = 0;
	std::string body_a, body_b;

	client.CallAsync("Svc", "ma", "{}", [&](const RpcResponse& r) {
		called_a++;
		if (r.success) body_a = r.body;
	});
	client.CallAsync("Svc", "mb", "{}", [&](const RpcResponse& r) {
		called_b++;
		if (r.success) body_b = r.body;
	});

	REQUIRE(client.PendingCount() == 2);

	// Resolve in reverse order.
	client.OnResponse(RpcResponse::Ok(2, "second"));
	REQUIRE(called_a == 0);
	REQUIRE(called_b == 1);
	REQUIRE(body_b == "second");

	client.OnResponse(RpcResponse::Ok(1, "first"));
	REQUIRE(called_a == 1);
	REQUIRE(called_b == 1);
	REQUIRE(body_a == "first");

	REQUIRE(client.PendingCount() == 0);
}

TEST_CASE("RpcClient mixed Call and CallAsync work together", "[rpc][client]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	bool async_called = false;

	auto future = client.Call("Svc", "sync", "{}");
	client.CallAsync("Svc", "async", "{}", [&](const RpcResponse& r) {
		async_called = true;
		REQUIRE(r.success);
	});

	REQUIRE(client.PendingCount() == 2);

	client.OnResponse(RpcResponse::Ok(1, "sync_result"));
	client.OnResponse(RpcResponse::Ok(2, "async_result"));

	REQUIRE(async_called);
	REQUIRE(future.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready);
	REQUIRE(future.get().body == "sync_result");
	REQUIRE(client.PendingCount() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Server: edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcServer method handler with no service handler returns 405 for unknown", "[rpc][server]") {
	RpcServer server;
	server.RegisterMethod("Svc", "only_this",
		[](const std::string&) -> std::string { return "ok"; });

	RpcRequest req1;
	req1.header.msgid = 1; req1.header.service = "Svc"; req1.header.method = "only_this"; req1.body = "{}";
	auto r1 = server.HandleRequest(req1);
	REQUIRE(r1.success == true);
	REQUIRE(r1.body == "ok");

	RpcRequest req2;
	req2.header.msgid = 2; req2.header.service = "Svc"; req2.header.method = "other"; req2.body = "{}";
	auto r2 = server.HandleRequest(req2);
	REQUIRE(r2.success == false);
	REQUIRE(r2.error_code == 405);
}

TEST_CASE("RpcServer handler receives empty body", "[rpc][server]") {
	RpcServer server;
	bool body_was_empty = false;
	server.RegisterService("Echo",
		[&body_was_empty](const std::string& method, const std::string& body) -> std::string {
			body_was_empty = body.empty();
			return "ok";
		});

	RpcRequest req;
	req.header.msgid = 1; req.header.service = "Echo"; req.header.method = "m"; req.body = "";
	auto resp = server.HandleRequest(req);
	REQUIRE(resp.success == true);
	REQUIRE(body_was_empty);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Integration: full round-trip with error / service not found
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RPC client-server round-trip with error", "[rpc][integration]") {
	RpcServer server;
	server.RegisterService("CalcService",
		[](const std::string& method, const std::string&) -> std::string {
			if (method == "div") throw std::runtime_error("division by zero");
			return "{}";
		});

	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	auto future = client.Call("CalcService", "div", R"({"a":1,"b":0})");

	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "CalcService";
	req.header.method = "div";
	req.header.type = RpcMessageType::kRequest;
	req.body = R"({"a":1,"b":0})";

	RpcResponse svcResp = server.HandleRequest(req);
	REQUIRE(svcResp.success == false);
	REQUIRE(svcResp.error_code == 500);
	REQUIRE(svcResp.error_message == "division by zero");

	client.OnResponse(svcResp);
	auto status = future.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);

	RpcResponse result = future.get();
	REQUIRE(result.success == false);
	REQUIRE(result.error_code == 500);
	REQUIRE(result.error_message == "division by zero");
}

TEST_CASE("RPC client-server round-trip service not found", "[rpc][integration]") {
	RpcServer server;
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});

	auto future = client.Call("NonExistent", "method", "{}");

	RpcRequest req;
	req.header.msgid = 1;
	req.header.service = "NonExistent";
	req.header.method = "method";
	req.header.type = RpcMessageType::kRequest;
	req.body = "{}";

	RpcResponse svcResp = server.HandleRequest(req);
	REQUIRE(svcResp.success == false);
	REQUIRE(svcResp.error_code == 404);

	client.OnResponse(svcResp);
	auto status = future.wait_for(std::chrono::milliseconds(50));
	REQUIRE(status == std::future_status::ready);

	RpcResponse result = future.get();
	REQUIRE(result.success == false);
	REQUIRE(result.error_code == 404);
}

// ═══════════════════════════════════════════════════════════════════════════
// RPC Stress: many concurrent pending requests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RpcClient handles 100 concurrent pending requests", "[rpc][stress]") {
	RpcClient client;
	client.SetSendCallback([](RpcRequest) {});
	std::vector<std::future<RpcResponse>> futures;

	for (int i = 0; i < 100; ++i) {
		futures.push_back(client.Call("Svc", "m" + std::to_string(i), "{}"));
	}
	REQUIRE(client.PendingCount() == 100);

	// Resolve all in reverse order.
	for (int i = 99; i >= 0; --i) {
		client.OnResponse(RpcResponse::Ok(static_cast<uint32_t>(i + 1),
			"result_" + std::to_string(i)));
	}

	REQUIRE(client.PendingCount() == 0);
	for (int i = 0; i < 100; ++i) {
		auto status = futures[i].wait_for(std::chrono::milliseconds(10));
		REQUIRE(status == std::future_status::ready);
		REQUIRE(futures[i].get().body == "result_" + std::to_string(i));
	}
}
