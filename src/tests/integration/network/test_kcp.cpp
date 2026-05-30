#include "wsa_init.h"
#include "log_init.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <runtime/evpp/kcp/kcp_server.h>
#include <runtime/evpp/kcp/sync_kcp_client.h>

namespace {

void ConfigureEcho(evpp::kcp::Server& server) {
	server.SetMessageHandler([](evpp::EventLoop*, evpp::kcp::MessagePtr& msg) {
		std::string body(msg->data(), msg->length());
		msg->Reply("ECHO:" + body);
	});
}

}  // namespace

TEST_CASE("KCP request response echo", "[integration][network][kcp]") {
	static const int kPort = 21335;

	evpp::kcp::Server server;
	ConfigureEcho(server);
	REQUIRE(server.Init(kPort));
	REQUIRE(server.Start());

	evpp::kcp::sync::Client client;
	REQUIRE(client.Connect("127.0.0.1", kPort, 0x11223344));

	std::string resp = client.DoRequest("hello_kcp", 3000);
	client.Close();
	server.Stop(true);

	REQUIRE(resp == "ECHO:hello_kcp");
}

TEST_CASE("KCP sessions isolate same conv from different clients", "[integration][network][kcp]") {
	static const int kPort = 21336;
	static const uint32_t kSharedConv = 0x44556677;

	evpp::kcp::Server server;
	ConfigureEcho(server);
	REQUIRE(server.Init(kPort));
	REQUIRE(server.Start());

	evpp::kcp::sync::Client c1;
	REQUIRE(c1.Connect("127.0.0.1", kPort, kSharedConv));
	REQUIRE(c1.DoRequest("client_one", 3000) == "ECHO:client_one");
	c1.Close();

	evpp::kcp::sync::Client c2;
	REQUIRE(c2.Connect("127.0.0.1", kPort, kSharedConv));
	std::string resp = c2.DoRequest("client_two", 3000);
	c2.Close();
	server.Stop(true);

	REQUIRE(resp == "ECHO:client_two");
}

TEST_CASE("KCP request response handles fragmented large messages", "[integration][network][kcp]") {
	static const int kPort = 21337;

	std::string payload(70000, 'x');
	for (size_t i = 0; i < payload.size(); ++i) {
		payload[i] = static_cast<char>('a' + (i % 26));
	}

	evpp::kcp::Server server;
	server.SetMaxMessageSize(256 * 1024);
	ConfigureEcho(server);
	REQUIRE(server.Init(kPort));
	REQUIRE(server.Start());

	evpp::kcp::sync::Client client;
	client.SetKcpWndSize(256, 256);
	REQUIRE(client.Connect("127.0.0.1", kPort, 0x8899aabb));

	std::string resp = client.DoRequest(payload, 5000);
	client.Close();
	server.Stop(true);

	REQUIRE(resp == "ECHO:" + payload);
}
