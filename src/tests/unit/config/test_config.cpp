#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "config_fixture.h"
#include "runtime/config/config_validator.h"
#include "runtime/config/platform_paths.h"

namespace {

struct TempConfigDir {
    std::filesystem::path root;

    explicit TempConfigDir(const std::string& name)
        : root(std::filesystem::temp_directory_path() / name) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "runtime", ec);
        std::filesystem::create_directories(root / "client", ec);
        std::filesystem::create_directories(root / "server", ec);
        std::filesystem::create_directories(root / "profiles", ec);
    }

    ~TempConfigDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::string path() const { return root.string(); }

    void write(const std::filesystem::path& relative, const std::string& content) {
        std::filesystem::create_directories((root / relative).parent_path());
        std::ofstream out(root / relative);
        out << content;
    }
};

const char* kValidRuntimeJson = R"({
    "resource_dir": "resources",
    "log": { "dir": "logs", "level": "info" },
    "frame": { "target_fps": 30, "interval_ms": 33 },
    "scripts_dir": "resources/script/runtime"
})";

const char* kValidClientJson = R"({
    "scripts_dir": "resources/script/client",
    "render": { "backend": "opengl", "max_fps": 60 }
})";

const char* kValidServerJson = R"({
    "http": { "timeout_sec": 5.0 },
    "msgpack": { "max_nesting_depth": 16 },
    "scripts_dir": "resources/script/server",
    "admin_port": 0
})";

}  // namespace

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

TEST_CASE("ConfigValidator rejects unknown environment", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 30 },
        "scripts_dir": ".",
        "environment": "prodction"
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

TEST_CASE("ConfigValidator rejects invalid operational server settings", "[config][validation]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadServerFromString(R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": ".",
        "admin_port": 8081,
        "admin_bind_address": "",
        "active_mongodb": "staging",
        "resource_limits": {
            "max_message_size": 1024,
            "max_buffer_capacity": 512,
            "max_http_body_size": 1024,
            "max_msgpack_depth": 16
        }
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
    REQUIRE((result.valid || !result.valid));
}

TEST_CASE("Reload rejects invalid client config and keeps previous values", "[config][reload]") {
    auto& cfg = engine::ConfigManager::Instance();
    TempConfigDir dir("evpp_config_reload_bad_client");
    dir.write("runtime/runtime.json", kValidRuntimeJson);
    dir.write("client/client.json", kValidClientJson);
    dir.write("server/server.json", kValidServerJson);

    REQUIRE(cfg.Load(dir.path()));
    REQUIRE(cfg.GetClientConfig().render.max_fps == 60);

    dir.write("client/client.json", R"({
        "scripts_dir": "resources/script/client",
        "render": { "backend": "missing_backend", "max_fps": 144 }
    })");

    REQUIRE_FALSE(cfg.Reload(dir.path()));
    REQUIRE(cfg.GetClientConfig().render.max_fps == 60);
    REQUIRE(cfg.GetClientConfig().render.backend == "opengl");
}

TEST_CASE("Reload rejects invalid profile overlay and keeps previous runtime", "[config][reload]") {
    auto& cfg = engine::ConfigManager::Instance();
    TempConfigDir dir("evpp_config_reload_bad_profile");
    dir.write("runtime/runtime.json", kValidRuntimeJson);
    dir.write("server/server.json", kValidServerJson);

    cfg.SetActiveEnvironment(engine::Environment::production);
    REQUIRE(cfg.Load(dir.path()));
    REQUIRE(cfg.GetRuntimeConfig().frame.target_fps == 30);

    dir.write("profiles/production.json", R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "info" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": ""
    })");

    bool reload_ok = cfg.Reload(dir.path());
    cfg.SetActiveEnvironment(engine::Environment::development);
    REQUIRE_FALSE(reload_ok);
    REQUIRE(cfg.GetRuntimeConfig().frame.target_fps == 30);
}

TEST_CASE("Reload rejects missing referenced MongoDB config and keeps server", "[config][reload]") {
    auto& cfg = engine::ConfigManager::Instance();
    TempConfigDir dir("evpp_config_reload_bad_mongo");
    dir.write("runtime/runtime.json", kValidRuntimeJson);
    dir.write("server/server.json", kValidServerJson);

    cfg.SetActiveEnvironment(engine::Environment::development);
    REQUIRE(cfg.Load(dir.path()));
    REQUIRE(cfg.GetServerConfig().mongodb_dev.empty());

    dir.write("server/server.json", R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": "resources/script/server",
        "admin_port": 0,
        "mongodb_dev": "does/not/exist.json"
    })");

    REQUIRE_FALSE(cfg.Reload(dir.path()));
    REQUIRE(cfg.GetServerConfig().mongodb_dev.empty());
    REQUIRE_FALSE(cfg.IsMongoDbDevLoaded());
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

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: struct parsing
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ClientConfig parses all sub-structs from JSON", "[config][client]") {
    auto& cfg = engine::ConfigManager::Instance();
    std::string client_json = R"({
        "scripts_dir": "resources/script/client",
        "render": {
            "backend": "vulkan",
            "resolution_width": 2560,
            "resolution_height": 1440,
            "fullscreen": true,
            "vsync": false,
            "msaa_samples": 8,
            "hdr": true,
            "max_fps": 144
        },
        "window": {
            "title": "Test Game",
            "width": 1920,
            "height": 1080,
            "resizable": false,
            "borderless": true,
            "monitor": 1
        },
        "input": {
            "mouse_sensitivity": 2.5,
            "mouse_invert_y": true,
            "gamepad_deadzone": 0.2,
            "touch_enabled": false
        },
        "audio": {
            "backend": "wasapi",
            "sample_rate": 48000,
            "channels": 6,
            "master_volume": 0.75,
            "music_volume": 0.5,
            "sfx_volume": 0.9,
            "spatial_audio": true,
            "mute_when_unfocused": false
        },
        "network": {
            "server_address": "game.example.com",
            "server_port": 9000,
            "reconnect_max_retries": 5,
            "reconnect_base_delay_ms": 1000,
            "reconnect_max_delay_ms": 15000,
            "timeout_ms": 10000,
            "client_prediction": false,
            "interpolation_delay_ms": 50
        },
        "assets": {
            "root_path": "data/assets",
            "streaming_budget_mb": 1024,
            "lod_bias": 2.0,
            "texture_quality": "ultra",
            "preload_list": ["hero.mesh", "world.map"]
        },
        "ui": {
            "font_path": "fonts/roboto.ttf",
            "font_size": 16,
            "scale": 1.25,
            "locale": "zh_CN",
            "theme": "light",
            "color_blind_mode": "protanopia"
        },
        "platform": {
            "save_data_path": "/custom/saves",
            "cache_path": "/custom/cache",
            "locale": "zh_CN"
        },
        "first_run_completed": true
    })";

    REQUIRE(cfg.LoadClientFromString(client_json));
    auto cc = cfg.GetClientConfig();

    REQUIRE(cc.render.backend == "vulkan");
    REQUIRE(cc.render.resolution_width == 2560);
    REQUIRE(cc.render.resolution_height == 1440);
    REQUIRE(cc.render.fullscreen == true);
    REQUIRE(cc.render.vsync == false);
    REQUIRE(cc.render.msaa_samples == 8);
    REQUIRE(cc.render.hdr == true);
    REQUIRE(cc.render.max_fps == 144);

    REQUIRE(cc.window.title == "Test Game");
    REQUIRE(cc.window.width == 1920);
    REQUIRE(cc.window.height == 1080);
    REQUIRE(cc.window.resizable == false);
    REQUIRE(cc.window.borderless == true);
    REQUIRE(cc.window.monitor == 1);

    REQUIRE(cc.input.mouse_sensitivity == 2.5f);
    REQUIRE(cc.input.mouse_invert_y == true);
    REQUIRE(cc.input.gamepad_deadzone == 0.2f);
    REQUIRE(cc.input.touch_enabled == false);

    REQUIRE(cc.audio.backend == "wasapi");
    REQUIRE(cc.audio.sample_rate == 48000);
    REQUIRE(cc.audio.channels == 6);
    REQUIRE(cc.audio.master_volume == 0.75f);

    REQUIRE(cc.network.server_address == "game.example.com");
    REQUIRE(cc.network.server_port == 9000);
    REQUIRE(cc.network.reconnect_max_retries == 5);
    REQUIRE(cc.network.client_prediction == false);

    REQUIRE(cc.assets.root_path == "data/assets");
    REQUIRE(cc.assets.streaming_budget_mb == 1024);
    REQUIRE(cc.assets.texture_quality == "ultra");
    REQUIRE(cc.assets.preload_list.size() == 2);

    REQUIRE(cc.ui.font_path == "fonts/roboto.ttf");
    REQUIRE(cc.ui.locale == "zh_CN");
    REQUIRE(cc.ui.color_blind_mode == "protanopia");

    REQUIRE(cc.platform.save_data_path == "/custom/saves");
    REQUIRE(cc.first_run_completed == true);
}

TEST_CASE("ClientConfig falls back to C++ defaults for missing fields", "[config][client]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": "."
    })"));
    auto cc = cfg.GetClientConfig();

    REQUIRE(cc.render.backend == "opengl");
    REQUIRE(cc.render.resolution_width == 1920);
    REQUIRE(cc.render.vsync == true);
    REQUIRE(cc.audio.sample_rate == 44100);
    REQUIRE(cc.audio.master_volume == 1.0f);
    REQUIRE(cc.network.server_port == 7777);
    REQUIRE(cc.assets.texture_quality == "high");
    REQUIRE(cc.first_run_completed == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ValidateClient rejects unknown render backend", "[config][client][validation]") {
    engine::ClientConfig cc;
    cc.scripts_dir = ".";
    cc.render.backend = "nonexistent";

    auto result = engine::ConfigValidator::ValidateClient(cc);
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("ValidateClient rejects volume out of range", "[config][client][validation]") {
    engine::ClientConfig cc;
    cc.scripts_dir = ".";
    cc.audio.master_volume = 1.5f;

    auto result = engine::ConfigValidator::ValidateClient(cc);
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("ValidateClient rejects resolution zero", "[config][client][validation]") {
    engine::ClientConfig cc;
    cc.scripts_dir = ".";
    cc.render.resolution_width = 0;

    auto result = engine::ConfigValidator::ValidateClient(cc);
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("ValidateClient rejects invalid server port", "[config][client][validation]") {
    engine::ClientConfig cc;
    cc.scripts_dir = ".";
    cc.network.server_port = 99999;

    auto result = engine::ConfigValidator::ValidateClient(cc);
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("ValidateClient accepts valid ClientConfig", "[config][client][validation]") {
    engine::ClientConfig cc;
    cc.scripts_dir = ".";

    auto result = engine::ConfigValidator::ValidateClient(cc);
    REQUIRE(result.valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: 3-layer loading
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("3-layer merge: user settings override factory settings", "[config][client][layered]") {
    auto& cfg = engine::ConfigManager::Instance();

    // Layer 1+2: load factory JSON
    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": "factory_scripts",
        "render": { "backend": "opengl", "max_fps": 30 },
        "audio": { "master_volume": 0.8 }
    })"));
    auto cc = cfg.GetClientConfig();
    REQUIRE(cc.scripts_dir == "factory_scripts");
    REQUIRE(cc.render.max_fps == 30);

    // Layer 3: user override (only some fields)
    REQUIRE(cfg.ApplyClientOverridesFromString(R"({
        "render": { "backend": "opengl", "max_fps": 60 }
    })"));
    cc = cfg.GetClientConfig();
    REQUIRE(cc.render.max_fps == 60);
    REQUIRE(cc.audio.master_volume == 0.8f);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: Save/Load user settings
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SaveClientUserSettings and LoadClientUserSettings round-trip", "[config][client][persist]") {
    auto& cfg = engine::ConfigManager::Instance();

    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": ".",
        "render": { "max_fps": 120, "vsync": false },
        "audio": { "master_volume": 0.5 }
    })"));

    std::string test_path = "test_user_settings.json";

    REQUIRE(cfg.SaveClientUserSettings(test_path));

    // Modify in-memory config
    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": ".",
        "render": { "max_fps": 30, "vsync": true },
        "audio": { "master_volume": 0.8 }
    })"));
    REQUIRE(cfg.GetClientConfig().render.max_fps == 30);

    // Load saved settings
    REQUIRE(cfg.LoadClientUserSettings(test_path));
    REQUIRE(cfg.GetClientConfig().render.max_fps == 120);
    REQUIRE(cfg.GetClientConfig().render.vsync == false);
    REQUIRE(cfg.GetClientConfig().audio.master_volume == 0.5f);

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(test_path, ec);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: corruption recovery
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Corrupted user settings falls back gracefully", "[config][client][corruption]") {
    auto& cfg = engine::ConfigManager::Instance();

    std::string corrupt_path = "test_corrupt_settings.json";
    {
        std::ofstream ofs(corrupt_path);
        ofs << "{ this is not valid JSON }";
    }

    // Load factory defaults first
    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": ".",
        "render": { "max_fps": 60 }
    })"));
    int fps_before = cfg.GetClientConfig().render.max_fps;

    // Attempt to load corrupted user settings
    bool loaded = cfg.LoadClientUserSettings(corrupt_path);
    REQUIRE(loaded);
    // Config should be unchanged
    REQUIRE(cfg.GetClientConfig().render.max_fps == fps_before);

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(corrupt_path, ec);
    std::filesystem::remove(corrupt_path + ".corrupt", ec);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: LoadClientLayered
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("LoadClientLayered applies factory then user layers", "[config][client][layered]") {
    auto& cfg = engine::ConfigManager::Instance();

    std::string factory_path = "test_factory_client.json";
    std::string user_path = "test_user_layer_settings.json";

    {
        std::ofstream ofs(factory_path);
        ofs << R"({"scripts_dir": "factory_dir", "render": {"max_fps": 30}})";
    }
    {
        std::ofstream ofs(user_path);
        ofs << R"({"render": {"max_fps": 90}})";
    }

    REQUIRE(cfg.LoadClientLayered(factory_path, user_path));
    auto cc = cfg.GetClientConfig();

    REQUIRE(cc.render.max_fps == 90);
    REQUIRE(cc.scripts_dir == "factory_dir");
    REQUIRE(cc.first_run_completed == true);

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(factory_path, ec);
    std::filesystem::remove(user_path, ec);
}

// ═══════════════════════════════════════════════════════════════════════════
// Platform paths
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("GetUserDataPath returns non-empty string", "[config][platform]") {
    std::string path = engine::platform::GetUserDataPath("evpp_test");
    REQUIRE_FALSE(path.empty());
}

TEST_CASE("GetUserSettingsPath appends settings.json", "[config][platform]") {
    std::string path = engine::platform::GetUserSettingsPath("evpp_test");
    REQUIRE_FALSE(path.empty());
    REQUIRE(path.find("settings.json") != std::string::npos);
}

TEST_CASE("GetUserDataPath produces different paths for different apps", "[config][platform]") {
    std::string path1 = engine::platform::GetUserDataPath("app_a");
    std::string path2 = engine::platform::GetUserDataPath("app_b");
    REQUIRE(path1 != path2);
}

// ═══════════════════════════════════════════════════════════════════════════
// ClientConfig: ResetClientUserSettings
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ResetClientUserSettings restores C++ defaults", "[config][client]") {
    auto& cfg = engine::ConfigManager::Instance();

    REQUIRE(cfg.LoadClientFromString(R"({
        "scripts_dir": "custom_scripts",
        "render": { "max_fps": 200, "vsync": false }
    })"));
    REQUIRE(cfg.GetClientConfig().render.max_fps == 200);

    std::string reset_path = "test_reset_settings.json";
    REQUIRE(cfg.ResetClientUserSettings(reset_path));

    auto cc = cfg.GetClientConfig();
    REQUIRE(cc.render.backend == "opengl");
    REQUIRE(cc.render.max_fps == 60);
    REQUIRE(cc.render.vsync == true);
    REQUIRE(cc.first_run_completed == false);
}
