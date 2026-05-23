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
    int target_fps = 30;       // 0 = unlimited (use interval_ms instead)
    int interval_ms = 33;      // fallback when target_fps is 0
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

    // ── From JSON strings (text) ─────────────────────────────────────
    // Parse config directly from JSON strings. Useful for programmatic
    // configuration or when config comes from a database/network.
    // Returns false on parse error (current values are preserved).

    bool LoadEngineFromString(const std::string& json);
    bool LoadServerFromString(const std::string& json);
    bool LoadFromString(const std::string& engine_json,
                        const std::string& server_json);

    // ── From files ───────────────────────────────────────────────────
    // Load a single config file or both from a directory.
    // Returns false if the file is missing or malformed.

    bool LoadEngineFromFile(const std::string& path);
    bool LoadServerFromFile(const std::string& path);
    bool Load(const std::string& config_dir);

    // ── Reload ───────────────────────────────────────────────────────

    // Reload configs from disk. Returns false on failure (current values
    // are preserved).
    bool Reload(const std::string& config_dir);

    // ── Accessors ────────────────────────────────────────────────────

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
