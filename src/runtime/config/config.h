#pragma once

#include <string>

#include "runtime/config/config_constants.h"
#include "runtime/core/engine_api.h"

namespace engine {

//============================================================================
// Config structs — aggregates for glaze auto-reflection (C++23).
// JSON key names match struct member names (snake_case).
//============================================================================

struct LogConfig {
    std::string dir = config::kDefaultLogDir;
    std::string level = config::kDefaultLogLevel;
    int rotation_size_mb = config::kDefaultRotationSizeMb;
    int max_backup_files = config::kDefaultMaxBackupFiles;
    std::string format_pattern =
        "%(time) [%(thread_id)] [%(log_level_short_code)] [%(logger)] %(message)";

    // Time-based rotation (empty = disabled, size-based only)
    std::string rotation_frequency = "";        // "daily", "hourly", "minutely"
    int rotation_interval = 1;                  // interval for hourly/minutely
    std::string rotation_time_daily = "00:00";  // "HH:MM" for daily rotation
    std::string rotation_naming_scheme = "date_and_time";  // "index", "date", "date_and_time"

    // Multi-instance
    std::string logger_name = config::kDefaultLoggerName;
    std::string log_filename = "";
};

struct FrameConfig {
    int target_fps = config::kDefaultTargetFps;
    int interval_ms = config::kDefaultIntervalMs;
    int slow_threshold_multiplier = config::kDefaultSlowThresholdMultiplier;
};

// Runtime (engine-level) config — shared by both client and server.
// Loaded from resources/config/runtime/runtime.json.
struct RuntimeConfig {
    std::string resource_dir = config::kDefaultResourceDir;
    LogConfig log;
    FrameConfig frame;
    std::string scripts_dir = config::kDefaultRuntimeScriptsDir;
};

// Client config — loaded from resources/config/client/client.json.
struct ClientConfig {
    std::string scripts_dir = config::kDefaultClientScriptsDir;
};

struct HttpConfig {
    double timeout_sec = config::kDefaultHttpTimeoutSec;
};

struct MsgpackConfig {
    int max_nesting_depth = config::kDefaultMsgpackMaxNestingDepth;
};

// Server config — loaded from resources/config/server/server.json.
struct ServerConfig {
    HttpConfig http;
    MsgpackConfig msgpack;
    std::string scripts_dir = config::kDefaultServerScriptsDir;
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
