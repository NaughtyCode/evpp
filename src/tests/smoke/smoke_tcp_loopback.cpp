#include "wsa_init.h"
#include "log_init.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/tcp_conn.h>
#include <runtime/evpp/buffer.h>
#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_client.h>

// Smoke test: TCP server start → client connect → echo → stop.
// Uses a fixed port since listen_addr() returns the constructor string unchanged.

TEST_CASE("TCP echo loopback", "[smoke][network]") {
    evpp::EventLoop loop;

    bool server_got_msg = false;
    bool client_got_echo = false;
    std::string client_received;

    // Use a fixed high port unlikely to conflict
    static const int kTestPort = 19876;

    auto* server = new evpp::TCPServer(&loop,
        "127.0.0.1:" + std::to_string(kTestPort), "SmokeEcho", 1);
    server->SetMessageCallback([&server_got_msg](const evpp::TCPConnPtr& conn, evpp::Buffer* msg) {
        server_got_msg = true;
        std::string body(msg->data(), msg->length());
        conn->Send("ECHO:" + body);
    });

    REQUIRE(server->Init());
    REQUIRE(server->Start());

    auto* client = new evpp::TCPClient(&loop,
        "127.0.0.1:" + std::to_string(kTestPort), "SmokeClient");
    client->set_auto_reconnect(false);

    client->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            conn->Send("hello");
        }
    });

    client->SetMessageCallback([&](const evpp::TCPConnPtr&, evpp::Buffer* msg) {
        client_got_echo = true;
        client_received = std::string(msg->data(), msg->length());
        // Clean up inside the loop thread before stopping the loop.
        // Calling Disconnect/Stop after loop.Run() returns would
        // deadlock because RunInLoop queues on a stopped EventLoop.
        client->Disconnect();
        server->Stop();
        loop.Stop();
    });

    loop.RunAfter(3000.0, [&]() {
        // Timeout: clean up before stopping, same reason as above.
        if (!client_got_echo) {
            client->Disconnect();
            server->Stop();
        }
        loop.Stop();
    });

    client->Connect();
    loop.Run();

    delete client;
    delete server;

    REQUIRE(server_got_msg);
    REQUIRE(client_got_echo);
    REQUIRE(client_received == "ECHO:hello");
}
