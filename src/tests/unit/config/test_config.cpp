#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>
#include "config_fixture.h"

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: JSON string loading
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigManager loads RuntimeConfig from valid JSON", "[config][load]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto rt = f.cfg.GetRuntimeConfig();
    REQUIRE(rt.resource_dir == "resources");
    REQUIRE(rt.log.level == "debug");
    REQUIRE(rt.frame.target_fps == 30);
    REQUIRE(rt.frame.interval_ms == 33);
    REQUIRE(rt.scripts_dir == "resources/script/runtime");
}

TEST_CASE("ConfigManager loads ServerConfig from valid JSON", "[config][load]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto srv = f.cfg.GetServerConfig();
    REQUIRE(srv.http.timeout_sec == 5.0);
    REQUIRE(srv.msgpack.max_nesting_depth == 16);
    REQUIRE(srv.scripts_dir == "resources/script/server");
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: error paths
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigManager rejects invalid JSON", "[config][error]") {
    auto& cfg = engine::ConfigManager::Instance();

    REQUIRE_FALSE(cfg.LoadRuntimeFromString(""));
    REQUIRE_FALSE(cfg.LoadRuntimeFromString("not json"));
    REQUIRE_FALSE(cfg.LoadRuntimeFromString("{"));
    REQUIRE_FALSE(cfg.LoadRuntimeFromString("[]"));  // array, not object
}

TEST_CASE("ConfigManager fills defaults for missing fields", "[config][default]") {
    auto& cfg = engine::ConfigManager::Instance();
    // Missing "frame" and "resource_dir" — glaze fills in struct defaults.
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "log": { "dir": "." },
        "scripts_dir": "."
    })"));
    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.resource_dir == "resources");
    REQUIRE(rt.frame.target_fps > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: defaults
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigManager defaults — log config", "[config][default]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 60 },
        "scripts_dir": "."
    })"));

    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.log.rotation_size_mb > 0);
    REQUIRE(rt.resource_dir == ".");
}

TEST_CASE("ConfigManager defaults — frame config", "[config][default]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 60 },
        "scripts_dir": "."
    })"));

    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.frame.slow_threshold_multiplier >= 1);
}
