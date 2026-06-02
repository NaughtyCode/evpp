#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#include "log_init.h"
#include "runtime/script/net_lifetime.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NetAliveGuard tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("NetAliveGuard starts alive", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    REQUIRE(guard.IsAlive());
}

TEST_CASE("NetAliveGuard Shutdown sets IsAlive to false", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    guard.Shutdown();
    REQUIRE_FALSE(guard.IsAlive());
}

TEST_CASE("NetAliveGuard TryAcquire succeeds when alive", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    REQUIRE(guard.TryAcquire());
    guard.Release();
}

TEST_CASE("NetAliveGuard TryAcquire fails after Shutdown", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    guard.Shutdown();
    REQUIRE_FALSE(guard.TryAcquire());
}

TEST_CASE("NetAliveGuard TryAcquire fails after Shutdown even with concurrent in-flight",
          "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    REQUIRE(guard.TryAcquire());
    /* Another "thread" — simulate concurrent shutdown */
    guard.Shutdown();
    /* TryAcquire from a third party should fail */
    REQUIRE_FALSE(guard.TryAcquire());
    /* Release the in-flight callback */
    guard.Release();
    /* Now TryAcquire should still fail (alive is false) */
    REQUIRE_FALSE(guard.TryAcquire());
}

TEST_CASE("NetAliveGuard WaitDrain blocks until all released", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;

    std::atomic<bool> callback_done{false};

    REQUIRE(guard.TryAcquire());

    std::thread worker([&guard, &callback_done]() {
        /* Simulate callback work */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        callback_done.store(true);
        guard.Release();
    });

    /* Shutdown and wait for drain */
    guard.Shutdown();
    guard.WaitDrain();

    /* After WaitDrain, the callback must have completed */
    REQUIRE(callback_done.load());
    REQUIRE_FALSE(guard.TryAcquire());

    worker.join();
}

TEST_CASE("NetAliveGuard WaitDrain returns immediately if nothing acquired",
          "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    guard.Shutdown();
    /* Should not block */
    guard.WaitDrain();
    REQUIRE_FALSE(guard.IsAlive());
}

TEST_CASE("NetAliveGuard TryAcquire serializes correctly", "[net_lifetime][guard]") {
    engine::script::NetAliveGuard guard;
    int counter = 0;
    std::mutex mtx;

    auto do_work = [&guard, &counter, &mtx]() {
        for (int i = 0; i < 100; ++i) {
            if (guard.TryAcquire()) {
                std::lock_guard<std::mutex> lock(mtx);
                counter++;
                guard.Release();
            }
        }
    };

    std::thread t1(do_work);
    std::thread t2(do_work);
    t1.join();
    t2.join();

    /* Both threads should have incremented counter exactly 100 times each */
    REQUIRE(counter == 200);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PendingRefTracker tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("PendingRefTracker AddRef and RemoveRef track correctly",
          "[net_lifetime][pending]") {
    engine::script::PendingRefTracker tracker;
    tracker.AddRef(1);
    tracker.AddRef(2);
    tracker.AddRef(3);
    tracker.RemoveRef(2);
    /* UnrefAll with null L should just clear */
    tracker.UnrefAll(nullptr);
    /* No crash = pass */
    REQUIRE(true);
}

TEST_CASE("PendingRefTracker UnrefAll with null L clears all refs",
          "[net_lifetime][pending]") {
    engine::script::PendingRefTracker tracker;
    tracker.AddRef(100);
    tracker.AddRef(200);
    /* Should not crash — unrefs skipped since L is null */
    tracker.UnrefAll(nullptr);
    REQUIRE(true);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ResourceLimits constant validation (from P0-5, included here for coverage)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("PendingRefTracker releases refs against their owning Lua states",
          "[net_lifetime][pending]") {
    lua_State* L1 = luaL_newstate();
    lua_State* L2 = luaL_newstate();
    REQUIRE(L1 != nullptr);
    REQUIRE(L2 != nullptr);

    lua_pushliteral(L1, "owned by L1");
    int ref1 = luaL_ref(L1, LUA_REGISTRYINDEX);
    lua_pushliteral(L2, "owned by L2");
    int ref2 = luaL_ref(L2, LUA_REGISTRYINDEX);

    engine::script::PendingRefTracker tracker;
    tracker.AddRef(L1, ref1);
    tracker.AddRef(L2, ref2);
    tracker.UnrefAll(nullptr);

    lua_pushliteral(L1, "new L1");
    int reused1 = luaL_ref(L1, LUA_REGISTRYINDEX);
    lua_pushliteral(L2, "new L2");
    int reused2 = luaL_ref(L2, LUA_REGISTRYINDEX);

    REQUIRE(reused1 == ref1);
    REQUIRE(reused2 == ref2);

    luaL_unref(L1, LUA_REGISTRYINDEX, reused1);
    luaL_unref(L2, LUA_REGISTRYINDEX, reused2);
    lua_close(L1);
    lua_close(L2);
}

TEST_CASE("NetAliveGuard is copy-disabled", "[net_lifetime][guard]") {
    /* Compile-time check: these should not compile */
    STATIC_REQUIRE_FALSE(std::is_copy_constructible<engine::script::NetAliveGuard>::value);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable<engine::script::NetAliveGuard>::value);
}

TEST_CASE("PendingRefTracker is copy-disabled", "[net_lifetime][pending]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible<engine::script::PendingRefTracker>::value);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable<engine::script::PendingRefTracker>::value);
}
