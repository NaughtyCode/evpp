#pragma once

#include <chrono>
#include <memory>

#include "log_init.h"
#include <runtime/core/timer/timer_manager.h>
#include <catch2/catch_test_macros.hpp>

// Wraps TimerManager with helpers for deterministic testing.
// Uses the update(TimePoint) overload to inject time.
struct TimerFixture {
    engine::TimerManager& tm;
    engine::TimePoint virtual_now_;

    TimerFixture() : tm(engine::TimerManager::instance()) {
        // Ensure clean state — create fresh instance if needed
    }

    void Reset() {
        tm.shutdown();
        tm.initialize();
        virtual_now_ = tm.now();
    }

    // Advance time by `ms` milliseconds (cumulative) and fire expired timers.
    engine::TimerManager::UpdateResult AdvanceBy(std::chrono::milliseconds ms) {
        virtual_now_ += ms;
        return tm.update(virtual_now_);
    }

    // Create a one-shot timer that fires after `delay_ms`.
    engine::TimerId CreateTimeout(int delay_ms, std::function<void()> cb) {
        auto id = tm.create_simple_timer(std::move(cb));
        tm.start_timer_relative(id, std::chrono::milliseconds(delay_ms));
        return id;
    }

    // Create a repeating timer.
    engine::TimerId CreateInterval(int interval_ms, std::function<void()> cb) {
        auto id = tm.create_simple_timer(std::move(cb));
        tm.start_timer_relative(id, std::chrono::milliseconds(interval_ms),
                                engine::TimerMode::kRelative);
        return id;
    }

    void AssertActive(engine::TimerId id) {
        REQUIRE(tm.is_timer_active(id));
    }

    void AssertInactive(engine::TimerId id) {
        REQUIRE_FALSE(tm.is_timer_active(id));
    }
};
