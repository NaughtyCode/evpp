#pragma once

#include <string>

#include "runtime/core/engine_api.h"

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

    // Time-based rotation (empty = disabled, size-based only)
    std::string rotation_frequency = "";        // "daily", "hourly", "minutely"
    int rotation_interval = 1;                  // interval for hourly/minutely
    std::string rotation_time_daily = "00:00";  // "HH:MM" for daily rotation
    std::string rotation_naming_scheme = "date_and_time";  // "index", "date", "date_and_time"

    // Multi-instance
    std::string logger_name = "root";   // logger instance name (used as log file prefix when log_filename is empty)
    std::string log_filename = "";      // override log file prefix (empty = use logger_name)
};

struct FrameConfig {
    int target_fps = 30;       // 0 = unlimited (use interval_ms instead)
    int interval_ms = 33;      // fallback when target_fps is 0
    int slow_threshold_multiplier = 2;
};

// Runtime (engine-level) config — shared by both client and server.
// Loaded from resources/config/runtime/runtime.json.
struct RuntimeConfig {
    std::string resource_dir = "resources";
    LogConfig log;
    FrameConfig frame;
    std::string scripts_dir = "resources/script/runtime";
};

// Client config — loaded from resources/config/client/client.json.
struct ClientConfig {
    std::string scripts_dir = "resources/script/client";
};

struct HttpConfig {
    double timeout_sec = 10.0;
};

struct MsgpackConfig {
    int max_nesting_depth = 16;
};

// Server config — loaded from resources/config/server/server.json.
struct ServerConfig {
    HttpConfig http;
    MsgpackConfig msgpack;
    std::string scripts_dir = "resources/script/server";
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

    bool LoadRuntimeFromString(const std::string& json);
    bool LoadClientFromString(const std::string& json);
    bool LoadServerFromString(const std::string& json);

    // ── From files ───────────────────────────────────────────────────

    bool LoadRuntimeFromFile(const std::string& path);
    bool LoadClientFromFile(const std::string& path);
    bool LoadServerFromFile(const std::string& path);

    // Load all configs from a directory tree:
    //   {config_dir}/runtime/runtime.json
    //   {config_dir}/client/client.json   (optional)
    //   {config_dir}/server/server.json   (optional)
    bool Load(const std::string& config_dir);

    // ── Reload ───────────────────────────────────────────────────────

    // Reload configs from disk. Returns false on failure (current values
    // are preserved).
    bool Reload(const std::string& config_dir);

    // ── Accessors ────────────────────────────────────────────────────

    const RuntimeConfig& GetRuntimeConfig() const { return runtime_config_; }
    RuntimeConfig& GetRuntimeConfigMutable() { return runtime_config_; }

    const ClientConfig& GetClientConfig() const { return client_config_; }
    ClientConfig& GetClientConfigMutable() { return client_config_; }

    const ServerConfig& GetServerConfig() const { return server_config_; }
    ServerConfig& GetServerConfigMutable() { return server_config_; }

private:
    ConfigManager() = default;

    RuntimeConfig runtime_config_;
    ClientConfig client_config_;
    ServerConfig server_config_;
};

} // namespace engine
