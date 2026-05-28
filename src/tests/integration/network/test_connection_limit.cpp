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
		"127.0.0.1:" + std::to_string(kConnLimitPort), "LimitServer", 1);
	server->SetMaxConnections(2);
	REQUIRE(server->max_connections() == 2);
	REQUIRE(server->connection_count() == 0);

	REQUIRE(server->Init());
	REQUIRE(server->Start());

	// Connect two clients — both should succeed
	std::atomic<int> connected{0};
	std::atomic<bool> done{false};

	auto make_client = [&](int id) {
		auto* c = new evpp::TCPClient(&loop,
			"127.0.0.1:" + std::to_string(kConnLimitPort), "Client" + std::to_string(id));
		c->set_auto_reconnect(false);
		c->SetConnectionCallback([&, c](const evpp::TCPConnPtr& conn) {
			if (conn->IsConnected()) {
				connected++;
			} else {
				// Disconnected without connecting = rejected
			}
		});
		c->Connect();
		return c;
	};

	auto* c1 = make_client(1);
	auto* c2 = make_client(2);

	// Let connections establish
	std::this_thread::sleep_for(200ms);
	loop.RunOnce();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();

	// At this point both should be connected
	REQUIRE(connected.load() >= 2);

	// Third client should be rejected
	std::atomic<bool> c3_connected{false};
	std::atomic<bool> c3_disconnected{false};
	auto* c3 = new evpp::TCPClient(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort), "Client3");
	c3->set_auto_reconnect(false);
	c3->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
		if (conn->IsConnected()) {
			c3_connected = true;
		}
	});
	c3->Connect();

	std::this_thread::sleep_for(200ms);
	loop.RunOnce();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();

	// c3 should NOT be connected (limit=2 enforced)
	REQUIRE_FALSE(c3_connected.load());

	// Cleanup
	c3->Disconnect();
	c2->Disconnect();
	c1->Disconnect();
	server->Stop();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();
	loop.Stop();

	delete c3;
	delete c2;
	delete c1;
	delete server;
}

TEST_CASE("TCPServer connection count decrements on disconnect", "[integration][network][conn_limit]") {
	evpp::EventLoop loop;

	auto* server = new evpp::TCPServer(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountServer", 1);
	server->SetMaxConnections(2);
	REQUIRE(server->connection_count() == 0);

	REQUIRE(server->Init());
	REQUIRE(server->Start());

	// Connect one client
	std::atomic<bool> c1_connected{false};
	auto* c1 = new evpp::TCPClient(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountClient");
	c1->set_auto_reconnect(false);
	c1->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
		if (conn->IsConnected()) c1_connected = true;
	});
	c1->Connect();

	std::this_thread::sleep_for(200ms);
	loop.RunOnce();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();

	REQUIRE(c1_connected.load());

	// Disconnect and verify count goes back to 0
	c1->Disconnect();
	std::this_thread::sleep_for(200ms);
	loop.RunOnce();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();

	REQUIRE(server->connection_count() == 0);

	// Now a new client should be accepted (limit no longer reached)
	std::atomic<bool> c2_connected{false};
	auto* c2 = new evpp::TCPClient(&loop,
		"127.0.0.1:" + std::to_string(kConnLimitPort + 1), "CountClient2");
	c2->set_auto_reconnect(false);
	c2->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
		if (conn->IsConnected()) c2_connected = true;
	});
	c2->Connect();

	std::this_thread::sleep_for(200ms);
	loop.RunOnce();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();

	REQUIRE(c2_connected.load());

	// Cleanup
	c2->Disconnect();
	server->Stop();
	std::this_thread::sleep_for(100ms);
	loop.RunOnce();
	loop.Stop();

	delete c2;
	delete c1;
	delete server;
}

TEST_CASE("TCPServer default max connections is 10000", "[network][conn_limit]") {
	evpp::EventLoop loop;
	evpp::TCPServer server(&loop, "127.0.0.1:19999", "DefaultServer", 1);

	REQUIRE(server.max_connections() == 10000);
	REQUIRE(server.connection_count() == 0);
}
