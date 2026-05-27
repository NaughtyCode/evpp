#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>
#include "config_fixture.h"

// Smoke test: load configs from inline JSON strings, verify parsed values.

TEST_CASE("Load configs from JSON strings", "[smoke][config]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto rt = f.cfg.GetRuntimeConfig();
    REQUIRE(rt.resource_dir == "resources");
    REQUIRE(rt.log.level == "debug");
    REQUIRE(rt.frame.target_fps == 30);
    REQUIRE(rt.frame.interval_ms == 33);

    auto srv = f.cfg.GetServerConfig();
    REQUIRE(srv.http.timeout_sec == 5.0);
    REQUIRE(srv.msgpack.max_nesting_depth == 16);
}

TEST_CASE("Config parse failure preserves old values", "[smoke][config]") {
    auto& cfg = engine::ConfigManager::Instance();

    // Load valid config first
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "debug" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "resources/script/runtime"
    })"));

    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.frame.target_fps == 30);

    // Try loading garbage — should fail and preserve old value
    REQUIRE_FALSE(cfg.LoadRuntimeFromString("not json {{{"));

    rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.frame.target_fps == 30);  // preserved
}

TEST_CASE("Config defaults fill missing fields", "[smoke][config]") {
    auto& cfg = engine::ConfigManager::Instance();

    // Load minimal valid JSON — missing optional fields get defaults.
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 60 },
        "scripts_dir": "."
    })"));

    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.log.rotation_size_mb > 0);                  // default rotation size
    REQUIRE(rt.resource_dir == ".");                       // set explicitly
}
