#pragma once

#include <string>

#include "engine/engine_export.h"

namespace engine {

//============================================================================
// Config structs — aggregates for glaze auto-reflection (C++23).
// JSON key names match struct member names (snake_case).
//============================================================================

struct LogConfig {
    std::string dir = "logs";
    std::string level = "info";
    int rotation_size_mb = 100;
    int max_backup_files = 10;
    std::string format_pattern =
        "%(time) [%(thread_id)] [%(log_level_short_code)] [%(logger)] %(message)";
};

struct FrameConfig {
    int interval_ms = 33;
    int slow_threshold_multiplier = 2;
};

struct EngineConfig {
    LogConfig log;
    FrameConfig frame;
    std::string scripts_dir = "resources/script";
};

struct HttpConfig {
    double timeout_sec = 10.0;
};

struct MsgpackConfig {
    int max_nesting_depth = 16;
};

struct ServerConfig {
    HttpConfig http;
    MsgpackConfig msgpack;
};

//============================================================================
// ConfigManager — loads configs from JSON files at startup
//============================================================================

class ENGINE_API ConfigManager {
public:
    static ConfigManager& Instance();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // Load configs from the given directory (e.g. "resources/config").
    // Returns false if a required file is missing or malformed.
    bool Load(const std::string& config_dir);

    // Reload configs from disk. Returns false on failure (current values
    // are preserved).
    bool Reload(const std::string& config_dir);

    const EngineConfig& GetEngineConfig() const { return engine_config_; }
    EngineConfig& GetEngineConfigMutable() { return engine_config_; }

    const ServerConfig& GetServerConfig() const { return server_config_; }
    ServerConfig& GetServerConfigMutable() { return server_config_; }

private:
    ConfigManager() = default;

    EngineConfig engine_config_;
    ServerConfig server_config_;
};

} // namespace engine
