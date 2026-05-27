#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_client.h>
#include <catch2/catch_test_macros.hpp>

// Provides an echo server + client pair on localhost for integration tests.
// The server echoes back any received message prefixed with "ECHO:".
struct NetworkFixture {
    static constexpr int kDefaultTimeoutMs = 2000;

    std::unique_ptr<evpp::EventLoop> loop;
    evpp::TCPServer* server = nullptr;
    evpp::TCPClient* client = nullptr;

    int actual_port = 0;
    bool server_started = false;
    bool client_connected = false;
    std::string last_received;

    NetworkFixture() {
        loop = std::make_unique<evpp::EventLoop>();
    }

    ~NetworkFixture() {
        TearDown();
    }

    void TearDown() {
        if (client) {
            client->Disconnect();
            client.reset();
        }
        if (server) {
            server->Stop();
            server.reset();
        }
    }

    // Start an echo server on 127.0.0.1:0 (OS picks the port).
    // Calls `on_ready` after Init+Start succeeds.
    bool StartEchoServer(std::function<void(int assigned_port)> on_ready = nullptr) {
        server = new evpp::TCPServer(loop.get(), "127.0.0.1:0", "TestEcho", 1);

        server->SetMessageCallback([](const evpp::TCPConnPtr& conn, evpp::Buffer* msg) {
            std::string body(msg->data(), msg->length());
            std::string response = "ECHO:" + body;
            conn->Send(response);
        });

        if (!server->Init()) return false;
        if (!server->Start()) return false;

        // Extract assigned port from listening address
        auto addr = server->listen_addr();
        auto colon = addr.rfind(':');
        if (colon != std::string::npos) {
            actual_port = std::stoi(addr.substr(colon + 1));
        }

        server_started = true;
        if (on_ready) on_ready(actual_port);
        return true;
    }

    // Connect a client to the echo server.
    bool ConnectClient(std::function<void(const std::string&)> on_message = nullptr) {
        REQUIRE(server_started);
        std::string addr = "127.0.0.1:" + std::to_string(actual_port);
        client = new evpp::TCPClient(loop.get(), addr, "TestClient");

        client->SetConnectionCallback([this](const evpp::TCPConnPtr& conn) {
            client_connected = conn->IsConnected();
        });

        client->SetMessageCallback([this, on_message](const evpp::TCPConnPtr&, evpp::Buffer* msg) {
            last_received = std::string(msg->data(), msg->length());
            if (on_message) on_message(last_received);
        });

        client->Connect();
        return true;
    }

    // Send a message from the client.
    void Send(const std::string& data) {
        REQUIRE(client != nullptr);
        client->Send(data);
    }

    // Pump the event loop until `predicate` returns true or timeout.
    void PumpUntil(std::function<bool()> predicate, int timeout_ms = kDefaultTimeoutMs) {
        // Schedule a stop after timeout
        loop->RunAfter(timeout_ms / 1000.0, [this]() {
            loop->Stop();
        });

        while (!predicate() && loop->IsRunning()) {
            loop->RunOneLoop(10);
        }

        if (loop->IsRunning()) {
            loop->Stop();
        }
    }
};
