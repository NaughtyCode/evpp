#pragma once

#include <memory>
#include <runtime/evpp/event_loop.h>
#include <catch2/catch_test_macros.hpp>

// Provides a standalone EventLoop for tests.
// Does NOT call Run() — tests dispatch manually or use RunInLoop/RunAfter.
struct EventLoopFixture {
    std::unique_ptr<evpp::EventLoop> loop;

    EventLoopFixture() {
        loop = std::make_unique<evpp::EventLoop>();
    }

    ~EventLoopFixture() {
        if (loop && loop->IsRunning()) {
            loop->Stop();
        }
    }

    // Pump the event loop for up to `timeout_ms` milliseconds,
    // returning after all pending work is drained.
    void Pump(int timeout_ms) {
        loop->RunAfter(timeout_ms / 1000.0, [this]() {
            loop->Stop();
        });
        loop->Run();
    }
};
