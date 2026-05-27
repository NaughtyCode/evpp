#pragma once

#include <string>

#include <runtime/config/config.h>
#include <catch2/catch_test_macros.hpp>

// Provides a ConfigManager loaded with minimal test configs.
struct ConfigFixture {
    engine::ConfigManager& cfg;

    ConfigFixture() : cfg(engine::ConfigManager::Instance()) {}

    // Load configs from inline JSON strings.
    void LoadFromStrings() {
        std::string runtime_json = R"({
            "resource_dir": "resources",
            "log": { "dir": "logs", "level": "debug" },
            "frame": { "target_fps": 30, "interval_ms": 33 },
            "scripts_dir": "resources/script/runtime"
        })";
        REQUIRE(cfg.LoadRuntimeFromString(runtime_json));

        std::string server_json = R"({
            "http": { "timeout_sec": 5.0 },
            "msgpack": { "max_nesting_depth": 16 },
            "scripts_dir": "resources/script/server"
        })";
        REQUIRE(cfg.LoadServerFromString(server_json));
    }

    // Load configs from the real resource directory.
    bool LoadFromDisk(const std::string& config_dir = "resources/config") {
        return cfg.Load(config_dir);
    }
};
