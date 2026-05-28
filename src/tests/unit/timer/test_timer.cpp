#include <catch2/catch_test_macros.hpp>
#include "timer_fixture.h"

// ═══════════════════════════════════════════════════════════════════════════
// TimerManager: lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimerManager initialize and shutdown", "[timer][lifecycle]") {
    TimerFixture f;
    f.Reset();
    REQUIRE(f.tm.is_initialized());

    f.tm.shutdown();
    REQUIRE_FALSE(f.tm.is_initialized());
}

// ═══════════════════════════════════════════════════════════════════════════
// TimerManager: one-shot timers
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("One-shot timer fires after delay", "[timer][oneshot]") {
    TimerFixture f;
    f.Reset();

    int fired = 0;
    auto id = f.CreateTimeout(100, [&fired]() { fired++; });

    // Before expiry
    auto r1 = f.AdvanceBy(std::chrono::milliseconds(50));
    REQUIRE(fired == 0);
    REQUIRE(f.tm.is_timer_active(id));

    // At expiry
    auto r2 = f.AdvanceBy(std::chrono::milliseconds(60));
    REQUIRE(fired == 1);
    REQUIRE_FALSE(f.tm.is_timer_active(id));  // one-shot: auto-cancel
}

TEST_CASE("Cancel timer prevents fire", "[timer][oneshot]") {
    TimerFixture f;
    f.Reset();

    int fired = 0;
    auto id = f.CreateTimeout(100, [&fired]() { fired++; });

    f.tm.cancel_timer(id);
    f.AdvanceBy(std::chrono::milliseconds(200));
    REQUIRE(fired == 0);
    REQUIRE_FALSE(f.tm.is_timer_active(id));
}

TEST_CASE("One-shot timer destroys safely without start", "[timer][oneshot]") {
    TimerFixture f;
    f.Reset();

    auto id = f.tm.create_simple_timer([]() {});
    REQUIRE_FALSE(f.tm.is_timer_active(id));
    f.tm.destroy_timer(id);
    REQUIRE(f.tm.timer_state(id) == engine::TimerState::kInactive);
}

// ═══════════════════════════════════════════════════════════════════════════
// TimerManager: repeating timers
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Repeating timer fires multiple times", "[timer][repeating]") {
    TimerFixture f;
    f.Reset();

    int count = 0;
    auto id = f.tm.create_repeating_simple_timer(
        std::chrono::milliseconds(100), [&count]() { count++; });
    f.tm.start_timer_relative(id, std::chrono::milliseconds(100));

    f.AdvanceBy(std::chrono::milliseconds(110));
    REQUIRE(count == 1);

    f.AdvanceBy(std::chrono::milliseconds(100));
    REQUIRE(count == 2);

    f.AdvanceBy(std::chrono::milliseconds(100));
    REQUIRE(count == 3);

    f.tm.cancel_timer(id);
}

TEST_CASE("Repeating timer cancel stops further fires", "[timer][repeating]") {
    TimerFixture f;
    f.Reset();

    int count = 0;
    engine::TimerId id = 0;
    id = f.tm.create_repeating_simple_timer(
        std::chrono::milliseconds(100), [&count, &f, &id]() {
            count++;
            if (count >= 2) {
                f.tm.cancel_timer(id);
            }
        });
    f.tm.start_timer_relative(id, std::chrono::milliseconds(100));

    f.AdvanceBy(std::chrono::milliseconds(110));
    REQUIRE(count == 1);

    f.AdvanceBy(std::chrono::milliseconds(100));
    REQUIRE(count == 2);  // callback fires, cancels itself

    f.AdvanceBy(std::chrono::milliseconds(100));
    REQUIRE(count == 2);  // no more fires
}

// ═══════════════════════════════════════════════════════════════════════════
// TimerManager: UpdateResult statistics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UpdateResult counts fired timers", "[timer][stats]") {
    TimerFixture f;
    f.Reset();

    f.CreateTimeout(10, []() {});
    f.CreateTimeout(10, []() {});
    f.CreateTimeout(10, []() {});

    auto result = f.AdvanceBy(std::chrono::milliseconds(20));
    REQUIRE(result.total_fired == 3);
}

TEST_CASE("Stats reflect active/total counts", "[timer][stats]") {
    TimerFixture f;
    f.Reset();

    auto id1 = f.CreateTimeout(100, []() {});
    auto id2 = f.CreateTimeout(200, []() {});

    auto stats = f.tm.stats();
    REQUIRE(stats.active_hrtimers == 2);

    f.AdvanceBy(std::chrono::milliseconds(150));
    stats = f.tm.stats();
    REQUIRE(stats.active_hrtimers == 1);

    f.tm.cancel_timer(id2);
}

// ═══════════════════════════════════════════════════════════════════════════
// TimerManager: concurrent access (basic)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Create and destroy many timers", "[timer][stress]") {
    TimerFixture f;
    f.Reset();

    std::vector<engine::TimerId> ids;
    for (int i = 0; i < 1000; ++i) {
        ids.push_back(f.tm.create_simple_timer([]() {}));
    }
    REQUIRE(ids.size() == 1000);

    auto stats = f.tm.stats();
    REQUIRE(stats.total_hrtimers == 1000);

    for (auto id : ids) {
        f.tm.destroy_timer(id);
    }
}
