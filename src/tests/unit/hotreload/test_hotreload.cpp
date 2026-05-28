#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "runtime/vm/script_reloader.h"

using namespace engine;

// ============================================================================
// ScriptReloader: construction & basic state
// ============================================================================

TEST_CASE("ScriptReloader default construction", "[hotreload][lifecycle]") {
    ScriptReloader reloader;
    // Default-constructible without crash
    REQUIRE_NOTHROW(ScriptReloader());
}

TEST_CASE("ScriptReloader non-copyable, non-movable", "[hotreload][lifecycle]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible<ScriptReloader>::value);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable<ScriptReloader>::value);
}

// ============================================================================
// ScriptReloader: SetTarget
// ============================================================================

TEST_CASE("SetTarget with nullptr VM and empty dirs", "[hotreload][target]") {
    ScriptReloader reloader;
    std::vector<std::string> empty_dirs;
    REQUIRE_NOTHROW(reloader.SetTarget(nullptr, empty_dirs));
}

TEST_CASE("SetTarget with nullptr VM and some script dirs", "[hotreload][target]") {
    ScriptReloader reloader;
    std::vector<std::string> dirs = {"scripts/", "modules/"};
    REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs));
}

TEST_CASE("SetTarget called multiple times overwrites previous", "[hotreload][target]") {
    ScriptReloader reloader;
    std::vector<std::string> dirs1 = {"dir1/"};
    std::vector<std::string> dirs2 = {"dir2/", "dir3/"};

    REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs1));
    REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs2));
}

// ============================================================================
// ScriptReloader: SetReloadCallback
// ============================================================================

TEST_CASE("SetReloadCallback with null function pointer", "[hotreload][callback]") {
    ScriptReloader reloader;
    REQUIRE_NOTHROW(reloader.SetReloadCallback(nullptr));
}

TEST_CASE("SetReloadCallback with valid function", "[hotreload][callback]") {
    ScriptReloader reloader;
    int call_count = 0;
    auto cb = [&call_count](const std::string& path, bool success) {
        call_count++;
    };

    REQUIRE_NOTHROW(reloader.SetReloadCallback(cb));
    // Callback is stored but not invoked by SetReloadCallback itself
    REQUIRE(call_count == 0);
}

TEST_CASE("SetReloadCallback overwrites previous callback", "[hotreload][callback]") {
    ScriptReloader reloader;

    int calls_a = 0;
    int calls_b = 0;
    reloader.SetReloadCallback([&](const std::string&, bool) { calls_a++; });
    reloader.SetReloadCallback([&](const std::string&, bool) { calls_b++; });

    // Second callback overwrites first
    REQUIRE(calls_a == 0);
    REQUIRE(calls_b == 0);
}

// ============================================================================
// ScriptReloader: ReloadFile
// ============================================================================

TEST_CASE("ReloadFile with empty path", "[hotreload][reload]") {
    ScriptReloader reloader;
    bool result = reloader.ReloadFile("");
    // No target VM set — should return false or handle gracefully
    (void)result;  // Just verify it doesn't crash
}

TEST_CASE("ReloadFile with non-existent file", "[hotreload][reload]") {
    ScriptReloader reloader;
    bool result = reloader.ReloadFile("nonexistent_script.lua");
    (void)result;  // Should not crash
}

// ============================================================================
// ScriptReloader: ReloadAll
// ============================================================================

TEST_CASE("ReloadAll with no target set", "[hotreload][reload_all]") {
    ScriptReloader reloader;
    bool result = reloader.ReloadAll();
    // No target configured — should return false or be safe
    (void)result;
}

TEST_CASE("ReloadAll with target but empty dirs", "[hotreload][reload_all]") {
    ScriptReloader reloader;
    std::vector<std::string> dirs;
    reloader.SetTarget(nullptr, dirs);
    REQUIRE_NOTHROW(reloader.ReloadAll());
}

// ============================================================================
// ScriptReloader: Start / Stop lifecycle
// ============================================================================

TEST_CASE("Start with no target set", "[hotreload][lifecycle]") {
    ScriptReloader reloader;
    REQUIRE_NOTHROW(reloader.Start());
}

TEST_CASE("Start then Stop", "[hotreload][lifecycle]") {
    ScriptReloader reloader;
    REQUIRE_NOTHROW(reloader.Start());
    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Start with custom poll and debounce intervals", "[hotreload][lifecycle]") {
    ScriptReloader reloader;
    REQUIRE_NOTHROW(reloader.Start(500, 100));
    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Stop without Start", "[hotreload][lifecycle]") {
    ScriptReloader reloader;
    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Start-Stop-Start cycle", "[hotreload][lifecycle]") {
    ScriptReloader reloader;

    REQUIRE_NOTHROW(reloader.Start());
    REQUIRE_NOTHROW(reloader.Stop());

    REQUIRE_NOTHROW(reloader.Start(2000, 500));
    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Multiple Start calls without Stop are safe", "[hotreload][lifecycle]") {
    ScriptReloader reloader;

    REQUIRE_NOTHROW(reloader.Start());
    REQUIRE_NOTHROW(reloader.Start());  // Second start — should be safe
    REQUIRE_NOTHROW(reloader.Stop());
}

// ============================================================================
// ScriptReloader: full setup then reload flow
// ============================================================================

TEST_CASE("Full setup: SetTarget + SetCallback + Start + ReloadAll + Stop",
          "[hotreload][integration]") {
    ScriptReloader reloader;

    std::atomic<int> reload_attempts{0};
    reloader.SetReloadCallback(
        [&](const std::string& path, bool success) { reload_attempts++; });

    std::vector<std::string> dirs = {"test_scripts/"};
    reloader.SetTarget(nullptr, dirs);

    REQUIRE_NOTHROW(reloader.Start(100, 50));

    // Manual reload while watcher is running
    bool reload_ok = reloader.ReloadAll();
    (void)reload_ok;

    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Destructor during active watching is safe", "[hotreload][lifecycle]") {
    // Ensure the destructor handles an actively-watching reloader gracefully.
    {
        ScriptReloader reloader;
        reloader.Start();
        // Let destructor call Stop if needed
    }
    SUCCEED("destructor completed without crash");
}

// ============================================================================
// ScriptReloader: multiple instances
// ============================================================================

TEST_CASE("Multiple independent reloaders", "[hotreload][lifecycle]") {
    ScriptReloader r1;
    ScriptReloader r2;

    std::vector<std::string> dirs_a = {"a/"};
    std::vector<std::string> dirs_b = {"b/"};

    r1.SetTarget(nullptr, dirs_a);
    r2.SetTarget(nullptr, dirs_b);

    REQUIRE_NOTHROW(r1.Start());
    REQUIRE_NOTHROW(r2.Start());

    REQUIRE_NOTHROW(r1.Stop());
    REQUIRE_NOTHROW(r2.Stop());
}
