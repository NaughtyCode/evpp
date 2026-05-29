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
    REQUIRE(rt.sandbox_level == "strict");
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
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "log": { "dir": "." },
        "scripts_dir": "."
    })"));
    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.resource_dir == "resources");
    REQUIRE(rt.frame.target_fps > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigValidator: RuntimeConfig validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigValidator rejects target_fps out of range", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 2000 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects interval_ms out of range", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30, "interval_ms": 0 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects empty resource_dir", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": "",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects empty scripts_dir", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ""
    })"));
}

TEST_CASE("ConfigValidator rejects rotation_size_mb out of range", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": ".", "rotation_size_mb": 99999 },
        "frame": { "target_fps": 30 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects unknown sandbox_level", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "sandbox_level": "stirct"
    })"));
}

TEST_CASE("ConfigValidator accepts known sandbox levels", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "sandbox_level": "full"
    })"));
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "sandbox_level": "server"
    })"));
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "sandbox_level": "strict"
    })"));
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigValidator: ServerConfig validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigValidator rejects admin_port out of range", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": ".",
        "admin_port": 99999
    })"));
}

TEST_CASE("ConfigValidator accepts admin_port = 0 for disabled", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": ".",
        "admin_port": 0
    })"));
}

TEST_CASE("ConfigValidator rejects http.timeout_sec = 0", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects msgpack.max_payload_size = 0", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16, "max_payload_size": 0 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator rejects msgpack.max_nesting_depth = 0", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 0 },
        "scripts_dir": "."
    })"));
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigValidator: cross-field validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigValidator warns on target_fps vs interval_ms mismatch", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    // target_fps=60 but interval_ms=33 (should be ~16)
    // This should still load (warning only), not fail
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 60, "interval_ms": 33 },
        "scripts_dir": "."
    })"));
}

TEST_CASE("ConfigValidator accepts valid target_fps vs interval_ms pair", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    // target_fps=30, interval_ms=33 (1000/30 ≈ 33)
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "."
    })"));
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

// ═══════════════════════════════════════════════════════════════════════════
// ConfigValidator: ValidateAll bulk validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ValidateAll combines errors from all configs", "[config][validation]") {
    engine::RuntimeConfig rt;
    rt.resource_dir = "";  // invalid
    engine::ClientConfig cc;
    engine::ServerConfig sc;
    sc.admin_port = 8081;
    sc.http.timeout_sec = 5.0;
    sc.msgpack.max_nesting_depth = 16;
    sc.scripts_dir = ".";

    auto result = engine::ConfigValidator::ValidateAll(rt, cc, sc);
    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ValidateAll returns valid for good config", "[config][validation]") {
    engine::RuntimeConfig rt;
    rt.resource_dir = ".";
    rt.scripts_dir = ".";
    rt.frame.target_fps = 30;
    rt.log.dir = ".";
    engine::ClientConfig cc;
    cc.scripts_dir = ".";
    engine::ServerConfig sc;
    sc.admin_port = 0;
    sc.http.timeout_sec = 5.0;
    sc.msgpack.max_nesting_depth = 16;
    sc.scripts_dir = ".";

    auto result = engine::ConfigValidator::ValidateAll(rt, cc, sc);
    REQUIRE(result.valid);
}

TEST_CASE("ValidateCross rejects admin_port enabled with empty scripts_dir", "[config][validation]") {
    engine::RuntimeConfig rt;
    rt.resource_dir = ".";
    rt.scripts_dir = "";  // empty — cross-field should catch
    rt.frame.target_fps = 30;
    rt.log.dir = ".";
    engine::ServerConfig sc;
    sc.admin_port = 8081;  // enabled
    sc.http.timeout_sec = 5.0;
    sc.msgpack.max_nesting_depth = 16;
    sc.scripts_dir = ".";

    auto result = engine::ConfigValidator::ValidateCross(rt, sc);
    REQUIRE_FALSE(result.valid);
}
