#include "wsa_init.h"
#include "log_init.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/udp/udp_server.h>

TEST_CASE("UDP server start and stop", "[integration][network][udp]") {
    evpp::udp::Server server;

    std::atomic<bool> msg_received{false};

    server.SetMessageHandler([&](evpp::EventLoop*, evpp::udp::MessagePtr&) {
        msg_received = true;
    });

    // Use port 0 for OS-assigned port
    REQUIRE(server.Init(0));
    REQUIRE(server.Start());
    REQUIRE(server.IsRunning());

    server.Stop(true);
    REQUIRE_FALSE(server.IsRunning());

    // No message should have arrived on an unused port
    REQUIRE_FALSE(msg_received);
}

TEST_CASE("UDP server init with port number", "[integration][network][udp]") {
    evpp::udp::Server server;

    server.SetMessageHandler([](evpp::EventLoop*, evpp::udp::MessagePtr&) {});

    REQUIRE(server.Init(21236));
    REQUIRE(server.Start());
    REQUIRE(server.IsRunning());

    server.Stop(true);
}
