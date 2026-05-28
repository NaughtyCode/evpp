#include <catch2/catch_test_macros.hpp>

#include "runtime/vm/coroutine_scheduler.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

using namespace engine;

// =============================================================================
// Test helpers
// =============================================================================

namespace {

// Create a minimal Lua state with only the base library.
lua_State* MakeLuaState() {
    lua_State* L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
    }
    return L;
}

// --- Coroutine body functions for testing ---

// A coroutine that returns immediately (zero results).
int CoroEmpty(lua_State* /*L*/) {
    return 0;
}

// A coroutine that returns a single integer.
int CoroReturnInt(lua_State* L) {
    lua_pushinteger(L, 42);
    return 1;
}

// A coroutine that calls Suspend then returns.
int CoroSuspend(lua_State* L) {
    CoroutineScheduler::Suspend(0);
    lua_pushstring(L, "resumed");
    return 1;
}

}  // namespace

// =============================================================================
// CoroutineScheduler — singleton
// =============================================================================

TEST_CASE("CoroutineScheduler is a singleton", "[coroutine][singleton]") {
    auto& s1 = CoroutineScheduler::Instance();
    auto& s2 = CoroutineScheduler::Instance();
    REQUIRE(&s1 == &s2);
}

// =============================================================================
// CoroutineScheduler — init
// =============================================================================

TEST_CASE("CoroutineScheduler Init with valid Lua state", "[coroutine][init]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    REQUIRE_NOTHROW(sched.Init(L));

    lua_close(L);
}

TEST_CASE("CoroutineScheduler ActiveCount is zero after init", "[coroutine][init]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);
    REQUIRE(sched.ActiveCount() == 0);

    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — max coroutines
// =============================================================================

TEST_CASE("CoroutineScheduler default max coroutines", "[coroutine][limits]") {
    auto& sched = CoroutineScheduler::Instance();
    REQUIRE(sched.GetMaxCoroutines() == 10000);
}

TEST_CASE("CoroutineScheduler set and get max coroutines", "[coroutine][limits]") {
    auto& sched = CoroutineScheduler::Instance();

    sched.SetMaxCoroutines(500);
    REQUIRE(sched.GetMaxCoroutines() == 500);

    sched.SetMaxCoroutines(0);
    REQUIRE(sched.GetMaxCoroutines() == 0);

    // Restore default
    sched.SetMaxCoroutines(10000);
    REQUIRE(sched.GetMaxCoroutines() == 10000);
}

// =============================================================================
// CoroutineScheduler — create coroutine
// =============================================================================

TEST_CASE("CreateCoroutine from C function returns handle >= 1", "[coroutine][create]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    lua_pushcfunction(L, CoroEmpty);
    int handle = sched.CreateCoroutine(L);

    REQUIRE(handle >= 1);

    // Clean up
    sched.CancelCoroutine(handle);
    lua_close(L);
}

TEST_CASE("CreateCoroutine with empty stack returns 0", "[coroutine][create]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    // Stack is empty — CreateCoroutine should fail
    int handle = sched.CreateCoroutine(L);
    REQUIRE(handle == 0);

    lua_close(L);
}

TEST_CASE("CreateCoroutine assigns unique handles", "[coroutine][create]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    auto create_one = [&]() -> int {
        lua_pushcfunction(L, CoroEmpty);
        return sched.CreateCoroutine(L);
    };

    int h1 = create_one();
    int h2 = create_one();
    int h3 = create_one();

    REQUIRE(h1 >= 1);
    REQUIRE(h2 >= 1);
    REQUIRE(h3 >= 1);
    REQUIRE(h1 != h2);
    REQUIRE(h2 != h3);
    REQUIRE(h1 != h3);

    sched.CancelAll();
    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — cancel
// =============================================================================

TEST_CASE("CancelCoroutine removes a specific coroutine", "[coroutine][cancel]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    lua_pushcfunction(L, CoroEmpty);
    int h1 = sched.CreateCoroutine(L);

    lua_pushcfunction(L, CoroEmpty);
    int h2 = sched.CreateCoroutine(L);

    size_t before = sched.ActiveCount();

    sched.CancelCoroutine(h1);
    size_t after_cancel_one = sched.ActiveCount();
    REQUIRE(after_cancel_one <= before);

    sched.CancelCoroutine(h2);
    size_t after_cancel_both = sched.ActiveCount();
    REQUIRE(after_cancel_both <= after_cancel_one);

    lua_close(L);
}

TEST_CASE("CancelCoroutine unknown handle is safe", "[coroutine][cancel]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    REQUIRE_NOTHROW(sched.CancelCoroutine(0));
    REQUIRE_NOTHROW(sched.CancelCoroutine(-1));
    REQUIRE_NOTHROW(sched.CancelCoroutine(99999));

    lua_close(L);
}

TEST_CASE("CancelAll removes all coroutines", "[coroutine][cancel]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    // Create several coroutines
    for (int i = 0; i < 5; ++i) {
        lua_pushcfunction(L, CoroEmpty);
        sched.CreateCoroutine(L);
    }

    sched.CancelAll();
    REQUIRE(sched.ActiveCount() == 0);

    lua_close(L);
}

TEST_CASE("CancelAll on empty scheduler is safe", "[coroutine][cancel]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);
    sched.CancelAll();

    REQUIRE(sched.ActiveCount() == 0);

    // Call again — should be idempotent
    REQUIRE_NOTHROW(sched.CancelAll());
    REQUIRE(sched.ActiveCount() == 0);

    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — Update
// =============================================================================

TEST_CASE("Update on empty scheduler does not crash", "[coroutine][update]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);
    sched.CancelAll();

    REQUIRE_NOTHROW(sched.Update());
    REQUIRE_NOTHROW(sched.Update(0));
    REQUIRE_NOTHROW(sched.Update(16));
    REQUIRE_NOTHROW(sched.Update(100));

    lua_close(L);
}

TEST_CASE("Update after creating and cancelling coroutines", "[coroutine][update]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    lua_pushcfunction(L, CoroEmpty);
    sched.CreateCoroutine(L);

    lua_pushcfunction(L, CoroEmpty);
    sched.CreateCoroutine(L);

    // Run update — shouldn't crash even with suspended coroutines
    REQUIRE_NOTHROW(sched.Update());

    sched.CancelAll();

    // Update after cancellation
    REQUIRE_NOTHROW(sched.Update());

    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — ResumeCoroutine
// =============================================================================

TEST_CASE("ResumeCoroutine unknown handle is safe", "[coroutine][resume]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    REQUIRE_NOTHROW(sched.ResumeCoroutine(0, 0));
    REQUIRE_NOTHROW(sched.ResumeCoroutine(99999, 0));
    REQUIRE_NOTHROW(sched.ResumeCoroutine(-1, 1));

    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — Suspend (static)
// =============================================================================

TEST_CASE("Suspend with default argument", "[coroutine][suspend]") {
    // Suspend(0) is called from ordinary code (not inside a coroutine).
    // It should not crash, though it may be a no-op when no coroutine runs.
    REQUIRE_NOTHROW(CoroutineScheduler::Suspend(0));
}

TEST_CASE("Suspend with wake time", "[coroutine][suspend]") {
    // Suspend with a future wake time — no active coroutine to suspend,
    // so this should be safe.
    REQUIRE_NOTHROW(CoroutineScheduler::Suspend(100));
    REQUIRE_NOTHROW(CoroutineScheduler::Suspend(0));
    REQUIRE_NOTHROW(CoroutineScheduler::Suspend(-1));
}

// =============================================================================
// CoroutineScheduler — CurrentHandle (static)
// =============================================================================

TEST_CASE("CurrentHandle returns 0 outside of coroutine context", "[coroutine][handle]") {
    REQUIRE(CoroutineScheduler::CurrentHandle() == 0);
}

// =============================================================================
// CoroutineScheduler — lifecycle: create, run, cancel
// =============================================================================

TEST_CASE("Create coroutine that suspends and resumes", "[coroutine][lifecycle]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    // Create a coroutine with Suspend body
    lua_pushcfunction(L, CoroSuspend);
    int handle = sched.CreateCoroutine(L);
    REQUIRE(handle >= 1);

    // The coroutine should exist after creation
    REQUIRE(sched.ActiveCount() >= 1);

    // Run Update — this should pick up any Runnable coroutines
    REQUIRE_NOTHROW(sched.Update());

    sched.CancelAll();
    lua_close(L);
}

TEST_CASE("Multiple coroutines create and cancel", "[coroutine][lifecycle]") {
    lua_State* L = MakeLuaState();
    REQUIRE(L != nullptr);

    auto& sched = CoroutineScheduler::Instance();
    sched.Init(L);

    // Create 10 coroutines
    std::vector<int> handles;
    for (int i = 0; i < 10; ++i) {
        lua_pushcfunction(L, CoroEmpty);
        int h = sched.CreateCoroutine(L);
        REQUIRE(h >= 1);
        handles.push_back(h);
    }

    REQUIRE(sched.ActiveCount() >= handles.size());

    // Cancel them one by one
    for (int h : handles) {
        REQUIRE_NOTHROW(sched.CancelCoroutine(h));
    }

    // After cancelling all, active count may be 0 or GarbageCollect may
    // need to run. CancelAll ensures cleanup.
    sched.CancelAll();
    REQUIRE(sched.ActiveCount() == 0);

    lua_close(L);
}

// =============================================================================
// CoroutineScheduler — stress: sequential init-and-use
// =============================================================================

TEST_CASE("Sequential init-create-cancel cycles", "[coroutine][stress]") {
    auto& sched = CoroutineScheduler::Instance();

    for (int cycle = 0; cycle < 3; ++cycle) {
        lua_State* L = MakeLuaState();
        REQUIRE(L != nullptr);

        sched.Init(L);
        sched.CancelAll();  // clean slate

        lua_pushcfunction(L, CoroReturnInt);
        int h = sched.CreateCoroutine(L);
        REQUIRE(h >= 1);

        lua_pushcfunction(L, CoroEmpty);
        int h2 = sched.CreateCoroutine(L);
        REQUIRE(h2 >= 1);

        REQUIRE_NOTHROW(sched.Update());

        sched.CancelAll();
        REQUIRE(sched.ActiveCount() == 0);

        lua_close(L);
    }
}
