#include "wsa_init.h"
#include "log_init.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <chrono>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/tcp_conn.h>
#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_client.h>

using namespace std::chrono_literals;

static const int kConnLimitPort = 19881;

/* ============================================================================
 * Integration: Connection limit enforcement (P2-21)
 * ============================================================================ */

TEST_CASE("TCPServer enforces max connections", "[integration][network][conn_limit]") {
	evpp::EventLoop loop;

	auto* server = new evpp::TCPServer(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort), "LimitServer", 0);
	server->SetMaxConnections(2);
	REQUIRE(server->max_connections() == 2);
	REQUIRE(server->connection_count() == 0);

	REQUIRE(server->Init());
	REQUIRE(server->Start());

	std::atomic<int> connected{0};

	auto make_client = [&](const std::string& name) {
		auto* c = new evpp::TCPClient(&loop,
			"127.0.0.1:" + std::to_string(kConnLimitPort), name);
		c->set_auto_reconnect(false);
		c->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
			if (conn->IsConnected()) {
				connected++;
			}
		});
		c->Connect();
		return c;
	};

	auto* c1 = make_client("Client1");
	auto* c2 = make_client("Client2");

	auto* c3 = new evpp::TCPClient(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort), "Client3");
	c3->set_auto_reconnect(false);
	c3->Connect();

	// Stop after a timeout — all cleanup must happen inside the loop thread
	loop.RunAfter(3000.0, [&]() {
		c3->Disconnect();
		c2->Disconnect();
		c1->Disconnect();
		server->Stop();
		loop.Stop();
	});

	loop.Run();

	// Verify server-side: at most 2 connections accepted
	REQUIRE(server->connection_count() <= 2);
	REQUIRE(connected.load() >= 2);

	delete c3;
	delete c2;
	delete c1;
	delete server;
}

TEST_CASE("TCPServer connection count decrements on disconnect", "[integration][network][conn_limit]") {
	evpp::EventLoop loop;

	auto* server = new evpp::TCPServer(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountServer", 0);
	server->SetMaxConnections(2);
	REQUIRE(server->connection_count() == 0);

	REQUIRE(server->Init());
	REQUIRE(server->Start());

	std::atomic<bool> c1_connected{false};
	std::atomic<int> count_after_disconnect{-1};
	std::atomic<bool> c2_connected{false};

	auto* c1 = new evpp::TCPClient(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountClient");
	c1->set_auto_reconnect(false);
	c1->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
		if (conn->IsConnected()) {
			c1_connected = true;
		}
	});
	c1->Connect();

	// c2 will be created in phase 2
	evpp::TCPClient* c2 = nullptr;

	// Phase 1 (t=1s): disconnect c1 after it has connected
	loop.RunAfter(1000.0, [&]() {
		c1->Disconnect();
	});

	// Phase 2 (t=2s): verify connection count dropped to 0, then connect c2
	loop.RunAfter(2000.0, [&]() {
		count_after_disconnect.store(server->connection_count());
		c2 = new evpp::TCPClient(&loop,
			"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountClient2");
		c2->set_auto_reconnect(false);
		c2->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
			if (conn->IsConnected()) c2_connected = true;
		});
		c2->Connect();
	});

	// Phase 3 (t=4s): clean up and stop
	loop.RunAfter(4000.0, [&]() {
		if (c2) c2->Disconnect();
		server->Stop();
		loop.Stop();
	});

	loop.Run();

	REQUIRE(c1_connected.load());
	REQUIRE(count_after_disconnect.load() == 0);
	REQUIRE(c2_connected.load());

	delete c2;
	delete c1;
	delete server;
}

TEST_CASE("TCPServer default max connections is 10000", "[network][conn_limit]") {
	evpp::EventLoop loop;
	evpp::TCPServer server(&loop, "127.0.0.1:19999", "DefaultServer", 0);

	REQUIRE(server.max_connections() == 10000);
	REQUIRE(server.connection_count() == 0);

	// Init and Start so the thread pool is properly initialized,
	// then stop cleanly to satisfy destructor preconditions.
	REQUIRE(server.Init());
	REQUIRE(server.Start());
	server.Stop();
	loop.RunAfter(100.0, [&]() { loop.Stop(); });
	loop.Run();
}
