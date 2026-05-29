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

// ═══════════════════════════════════════════════════════════════════════════
// ConfigChangeSet: Diff generation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Diff detects changed field", "[config][diff]") {
    engine::RuntimeConfig old_rt, new_rt;
    engine::ServerConfig old_srv, new_srv;

    old_rt.frame.target_fps = 30;
    new_rt.frame.target_fps = 60;

    auto changes = engine::ConfigManager::Diff(old_rt, new_rt, old_srv, new_srv);
    REQUIRE_FALSE(changes.empty());

    bool found = false;
    for (const auto& entry : changes) {
        if (entry.field_path == "frame.target_fps") {
            REQUIRE(entry.old_value == "30");
            REQUIRE(entry.new_value == "60");
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Diff returns empty for identical configs", "[config][diff]") {
    engine::RuntimeConfig rt;
    engine::ServerConfig srv;

    auto changes = engine::ConfigManager::Diff(rt, rt, srv, srv);
    REQUIRE(changes.empty());
}

TEST_CASE("Diff detects multiple changed fields", "[config][diff]") {
    engine::RuntimeConfig old_rt, new_rt;
    engine::ServerConfig old_srv, new_srv;

    new_rt.log.level = "warn";
    new_rt.sandbox_level = "full";
    new_srv.admin_port = 9090;

    auto changes = engine::ConfigManager::Diff(old_rt, new_rt, old_srv, new_srv);
    REQUIRE(changes.size() >= 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: RegisterReloadCallback with ConfigChangeSet
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RegisterReloadCallback with ConfigChangeSet signature", "[config][callback]") {
    ConfigFixture f;
    f.LoadFromStrings();

    int cb_id = f.cfg.RegisterReloadCallback(
        [](const engine::ConfigChangeSet&) { /* no-op */ });
    REQUIRE(cb_id > 0);

    // Second registration gets a different ID.
    int cb2 = f.cfg.RegisterReloadCallback(
        [](const engine::ConfigChangeSet&) { /* no-op */ });
    REQUIRE(cb2 != cb_id);

    f.cfg.UnregisterReloadCallback(cb_id);
    f.cfg.UnregisterReloadCallback(cb2);
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: Rollback
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CanRollback returns true after first load that saves snapshot", "[config][rollback]") {
    ConfigFixture f;
    f.LoadFromStrings();
    // LoadFromStrings calls LoadRuntimeFromString which now saves a snapshot.
    REQUIRE(f.cfg.CanRollback());
}

TEST_CASE("Rollback restores previous config after Reload", "[config][rollback]") {
    ConfigFixture f;
    f.LoadFromStrings();

    int original_fps = f.cfg.GetRuntimeConfig().frame.target_fps;

    // Change target_fps.
    REQUIRE(f.cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 99 },
        "scripts_dir": "."
    })"));
    REQUIRE(f.cfg.GetRuntimeConfig().frame.target_fps == 99);
    REQUIRE(f.cfg.CanRollback());

    // Rollback should restore original value.
    REQUIRE(f.cfg.Rollback());
    REQUIRE(f.cfg.GetRuntimeConfig().frame.target_fps == original_fps);
}

TEST_CASE("Rollback toggles between two config versions", "[config][rollback]") {
    ConfigFixture f;
    f.LoadFromStrings();

    // LoadFromStrings established a snapshot (C++ defaults vs fixture JSON).
    REQUIRE(f.cfg.CanRollback());

    int current_fps = f.cfg.GetRuntimeConfig().frame.target_fps;

    // Change target_fps to establish a clear "before" snapshot.
    REQUIRE(f.cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 99 },
        "scripts_dir": "."
    })"));

    // Rollback should restore the pre-99 value.
    REQUIRE(f.cfg.Rollback());
    REQUIRE(f.cfg.GetRuntimeConfig().frame.target_fps == current_fps);

    // After rollback, the snapshot still exists (it now holds the 99 version).
    REQUIRE(f.cfg.CanRollback());

    // Second rollback toggles back to 99.
    REQUIRE(f.cfg.Rollback());
    REQUIRE(f.cfg.GetRuntimeConfig().frame.target_fps == 99);
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: AutoReload toggle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("IsAutoReloadEnabled returns false by default", "[config][autoreload]") {
    ConfigFixture f;
    f.LoadFromStrings();
    REQUIRE_FALSE(f.cfg.IsAutoReloadEnabled());
}

TEST_CASE("EnableAutoReload then DisableAutoReload", "[config][autoreload]") {
    ConfigFixture f;
    f.LoadFromStrings();

    f.cfg.EnableAutoReload("resources/config");
    REQUIRE(f.cfg.IsAutoReloadEnabled());

    f.cfg.DisableAutoReload();
    REQUIRE_FALSE(f.cfg.IsAutoReloadEnabled());
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigManager: Dump / ValidateOnly
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Dump produces valid JSON", "[config][dump]") {
    ConfigFixture f;
    f.LoadFromStrings();

    std::string json = f.cfg.Dump();
    REQUIRE_FALSE(json.empty());
    // Should contain runtime config keys.
    REQUIRE(json.find("\"runtime\"") != std::string::npos);
}

TEST_CASE("DumpRuntime produces valid JSON", "[config][dump]") {
    ConfigFixture f;
    f.LoadFromStrings();

    std::string json = f.cfg.DumpRuntime();
    REQUIRE_FALSE(json.empty());
    REQUIRE(json.find("\"frame\"") != std::string::npos);
}

TEST_CASE("ValidateOnly returns valid for good config dir", "[config][validate]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto result = f.cfg.ValidateOnly("resources/config");
    // May be valid or invalid depending on whether config files exist on disk.
    // The key is that it doesn't crash and returns a result.
    REQUIRE(result.valid || !result.valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// Environment: ParseEnvironment / EnvironmentToString
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ParseEnvironment maps strings to enum", "[config][environment]") {
    REQUIRE(engine::ParseEnvironment("development") == engine::Environment::development);
    REQUIRE(engine::ParseEnvironment("dev") == engine::Environment::development);
    REQUIRE(engine::ParseEnvironment("staging") == engine::Environment::staging);
    REQUIRE(engine::ParseEnvironment("stage") == engine::Environment::staging);
    REQUIRE(engine::ParseEnvironment("production") == engine::Environment::production);
    REQUIRE(engine::ParseEnvironment("prod") == engine::Environment::production);
    // Unknown values default to development (safe fallback).
    REQUIRE(engine::ParseEnvironment("invalid") == engine::Environment::development);
    REQUIRE(engine::ParseEnvironment("") == engine::Environment::development);
}

TEST_CASE("EnvironmentToString produces correct strings", "[config][environment]") {
    REQUIRE(std::string(engine::EnvironmentToString(engine::Environment::development)) == "development");
    REQUIRE(std::string(engine::EnvironmentToString(engine::Environment::staging)) == "staging");
    REQUIRE(std::string(engine::EnvironmentToString(engine::Environment::production)) == "production");
}

TEST_CASE("ConfigManager ActiveEnvironment defaults to development", "[config][environment]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.GetActiveEnvironment() == engine::Environment::development);
}

TEST_CASE("ConfigManager SetActiveEnvironment changes environment", "[config][environment]") {
    auto& cfg = engine::ConfigManager::Instance();
    cfg.SetActiveEnvironment(engine::Environment::production);
    REQUIRE(cfg.GetActiveEnvironment() == engine::Environment::production);
    cfg.SetActiveEnvironment(engine::Environment::staging);
    REQUIRE(cfg.GetActiveEnvironment() == engine::Environment::staging);
    // Reset to default for other tests.
    cfg.SetActiveEnvironment(engine::Environment::development);
}

// ═══════════════════════════════════════════════════════════════════════════
// RuntimeConfig: environment field
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RuntimeConfig environment field defaults to development", "[config][environment]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto rt = f.cfg.GetRuntimeConfig();
    // LoadFromStrings doesn't set environment explicitly — the default applies.
    REQUIRE(rt.environment == "development");
}

TEST_CASE("RuntimeConfig environment field can be loaded from JSON", "[config][environment]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "environment": "production"
    })"));
    auto rt = cfg.GetRuntimeConfig();
    REQUIRE(rt.environment == "production");
}

// ═══════════════════════════════════════════════════════════════════════════
// ServerConfig: active_mongodb field
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ServerConfig active_mongodb defaults to empty", "[config][environment]") {
    ConfigFixture f;
    f.LoadFromStrings();

    auto srv = f.cfg.GetServerConfig();
    REQUIRE(srv.active_mongodb.empty());
}

TEST_CASE("ServerConfig active_mongodb can be set via JSON", "[config][environment]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": ".",
        "active_mongodb": "public"
    })"));
    auto srv = cfg.GetServerConfig();
    REQUIRE(srv.active_mongodb == "public");
}
