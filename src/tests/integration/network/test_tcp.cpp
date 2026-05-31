#include "wsa_init.h"
#include "log_init.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <chrono>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/tcp_conn.h>
#include <runtime/evpp/buffer.h>
#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_client.h>

using namespace std::chrono_literals;

// Fixed ports for integration tests — avoid conflicts with other services.
// Separate ports per test case to avoid Windows TIME_WAIT clashes when
// test cases run back-to-back.
static const int kIntTestPort1 = 19877;
static const int kIntTestPort2 = 19878;
static const int kIntTestPort3 = 19879;

// ═══════════════════════════════════════════════════════════════════════════
// Integration: TCP echo roundtrip
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TCP echo roundtrip", "[integration][network][tcp]") {
    evpp::EventLoop loop;

    std::atomic<bool> server_done{false};
    std::atomic<bool> client_done{false};
    std::string client_received;

    auto* server = new evpp::TCPServer(&loop,
        "127.0.0.1:" + std::to_string(kIntTestPort1), "IntEcho", 1);
    server->SetMessageCallback([](const evpp::TCPConnPtr& conn, evpp::Buffer* msg) {
        std::string body(msg->data(), msg->length());
        conn->Send("ECHO:" + body);
    });

    REQUIRE(server->Init());
    REQUIRE(server->Start());

    auto* client = new evpp::TCPClient(&loop,
        "127.0.0.1:" + std::to_string(kIntTestPort1), "IntClient");
    client->set_auto_reconnect(false);

    bool stop_requested = false;
    auto request_shutdown = [&]() {
        if (stop_requested) {
            return;
        }
        stop_requested = true;
        client->Disconnect();
        server->Stop([&]() {
            server_done = true;
            loop.Stop();
        });
    };

    client->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            conn->Send("integration_test");
        }
    });

    client->SetMessageCallback([&](const evpp::TCPConnPtr&, evpp::Buffer* msg) {
        client_received = std::string(msg->data(), msg->length());
        client_done = true;
        request_shutdown();
    });

    loop.RunAfter(3000.0, [&]() {
        if (!client_done) {
            request_shutdown();
        }
    });
    client->Connect();
    loop.Run();

    delete client;
    delete server;

    REQUIRE(server_done);
    REQUIRE(client_done);
    REQUIRE(client_received == "ECHO:integration_test");
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration: large message echo
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TCP large message echo", "[integration][network][tcp]") {
    evpp::EventLoop loop;

    std::atomic<bool> server_done{false};
    bool client_done = false;
    std::string client_received;
    std::string test_msg(4096, 'X');
    for (size_t i = 0; i < test_msg.size(); ++i)
        test_msg[i] = static_cast<char>('A' + (i % 26));

    auto* server = new evpp::TCPServer(&loop,
        "127.0.0.1:" + std::to_string(kIntTestPort2), "LargeMsg", 1);
    server->SetMessageCallback([](const evpp::TCPConnPtr& conn, evpp::Buffer* msg) {
        std::string body(msg->data(), msg->length());
        conn->Send("RE:" + body);
    });

    REQUIRE(server->Init());
    REQUIRE(server->Start());

    auto* client = new evpp::TCPClient(&loop,
        "127.0.0.1:" + std::to_string(kIntTestPort2), "LargeClient");
    client->set_auto_reconnect(false);

    bool stop_requested = false;
    auto request_shutdown = [&]() {
        if (stop_requested) {
            return;
        }
        stop_requested = true;
        client->Disconnect();
        server->Stop([&]() {
            server_done = true;
            loop.Stop();
        });
    };

    client->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            conn->Send(test_msg);
        }
    });

    client->SetMessageCallback([&](const evpp::TCPConnPtr&, evpp::Buffer* msg) {
        client_received = std::string(msg->data(), msg->length());
        client_done = true;
        request_shutdown();
    });

    loop.RunAfter(3000.0, [&]() {
        request_shutdown();
    });
    client->Connect();
    loop.Run();

    delete client;
    delete server;

    REQUIRE(server_done);
    REQUIRE(client_done);
    REQUIRE(client_received == "RE:" + test_msg);
}

TEST_CASE("TCPServer stop succeeds after EventLoop has exited", "[integration][network][tcp]") {
    evpp::EventLoop loop;

    auto* server = new evpp::TCPServer(&loop,
        "127.0.0.1:" + std::to_string(kIntTestPort3), "StoppedLoopServer", 0);
    REQUIRE(server->Init());
    REQUIRE(server->Start());

    loop.RunAfter(10.0, [&]() {
        loop.Stop();
    });
    loop.Run();

    bool stopped = false;
    server->Stop([&]() {
        stopped = true;
    });

    REQUIRE(stopped);
    REQUIRE(server->IsStopped());

    delete server;
}
