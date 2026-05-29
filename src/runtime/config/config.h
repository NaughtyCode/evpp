#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include <cstdlib>

#include <glaze/glaze.hpp>

#include "runtime/config/config_constants.h"
#include "runtime/config/i_config_manager.h"
#include "runtime/config/limits.h"
#include "runtime/core/engine_api.h"
#include "runtime/vm/file_watcher.h"

namespace engine {

// Environment — runtime deployment target.
// Replaces compile-time #ifndef NDEBUG for dev/prod behaviour.
enum class Environment { development, staging, production };

inline Environment ParseEnvironment(const std::string& str) {
    if (str == "production" || str == "prod") return Environment::production;
    if (str == "staging" || str == "stage") return Environment::staging;
    return Environment::development;  // "development", "dev", or unknown → safe default
}

inline const char* EnvironmentToString(Environment env) {
    switch (env) {
    case Environment::production: return "production";
    case Environment::staging:    return "staging";
    default:                       return "development";
    }
}

inline Environment EnvironmentFromEnvVar() {
    const char* val = std::getenv("EVPP_ENV");
    if (val && val[0] != '\0') return ParseEnvironment(val);
    return Environment::development;
}

// SandboxLevel — Lua VM sandbox security tier.
// Controls which standard libraries and dangerous functions are enabled.
enum class SandboxLevel { Strict, Server, Full };

inline SandboxLevel ParseSandboxLevel(const std::string& str) {
    if (str == "full")    return SandboxLevel::Full;
    if (str == "server")  return SandboxLevel::Server;
    return SandboxLevel::Strict;  // "strict" or unknown → safe default
}

inline const char* SandboxLevelToString(SandboxLevel level) {
    switch (level) {
    case SandboxLevel::Full:   return "full";
    case SandboxLevel::Server: return "server";
    default:                    return "strict";
    }
}

// ── TCP keepalive config ──

struct TcpKeepaliveConfig {
    int idle_sec     = config::kDefaultTcpKeepaliveIdleSec;
    int interval_sec = config::kDefaultTcpKeepaliveIntervalSec;
    int count        = config::kDefaultTcpKeepaliveCount;
};

// ── Instance identity (multi-instance / cluster awareness) ──

struct InstanceIdentity {
    std::string id;       // e.g. "game-server-01"
    std::string region;   // e.g. "us-east-1"
    std::string zone;     // e.g. "us-east-1a"
    std::string cluster;  // e.g. "primary"
};

// Forward declarations
struct DbServiceConfig;

// ConfigChangeEntry — describes a single field change in a config reload.
struct ConfigChangeEntry {
	std::string field_path;   // dotted path, e.g. "frame.target_fps"
	std::string old_value;    // stringified old value
	std::string new_value;    // stringified new value
};

using ConfigChangeSet = std::vector<ConfigChangeEntry>;

// Config structs — aggregates for glaze auto-reflection (C++23).
// JSON key names match struct member names (snake_case).

struct LogConfig {
	std::string dir = config::kDefaultLogDir;
	std::string level = config::kDefaultLogLevel;
	int rotation_size_mb = config::kDefaultRotationSizeMb;
	int max_backup_files = config::kDefaultMaxBackupFiles;
	std::string format_pattern =
		"%(time) [%(thread_id)] [%(log_level_short_code)] [%(short_source_location)] "
		"[%(caller_function)] [%(logger)] %(message)";

	// Time-based rotation (empty = disabled, size-based only)
	std::string rotation_frequency = "";  // "daily", "hourly", "minutely"
	int rotation_interval = 1;	// interval for hourly/minutely
	std::string rotation_time_daily = "00:00";	// "HH:MM" for daily rotation
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
	std::string sandbox_level = "strict";
	std::string environment = "development";  // deployment target (dev/staging/prod)
};

// ── Client config sub-structs ──────────────────────────────────────────────
// Each struct holds one subsystem of client-only configuration.
// All members have C++ defaults (Layer 1 of the 3-layer loading model).

// Render backend selection. Valid values match the RenderBackendX constants.
struct RenderConfig {
	std::string backend = "opengl";
	int resolution_width = 1920;
	int resolution_height = 1080;
	bool fullscreen = false;
	bool vsync = true;
	int msaa_samples = 4;
	bool hdr = false;
	int max_fps = 60;
};

struct WindowConfig {
	std::string title = "CloudEngine";
	int width = 1280;
	int height = 720;
	bool resizable = true;
	bool borderless = false;
	int monitor = 0;  // 0 = primary
};

struct InputConfig {
	float mouse_sensitivity = 1.0f;
	bool mouse_invert_y = false;
	float gamepad_deadzone = 0.15f;
	bool touch_enabled = true;
};

struct AudioConfig {
	std::string backend = "openal";
	int sample_rate = 44100;
	int channels = 2;  // stereo
	float master_volume = 1.0f;
	float music_volume = 0.8f;
	float sfx_volume = 1.0f;
	bool spatial_audio = false;
	bool mute_when_unfocused = true;
};

struct NetworkClientConfig {
	std::string server_address = "127.0.0.1";
	int server_port = 7777;
	int reconnect_max_retries = 10;
	int reconnect_base_delay_ms = 500;
	int reconnect_max_delay_ms = 30000;
	int timeout_ms = 5000;
	bool client_prediction = true;
	int interpolation_delay_ms = 100;
};

struct AssetConfig {
	std::string root_path = "resources/assets";
	int streaming_budget_mb = 512;
	float lod_bias = 1.0f;
	std::string texture_quality = "high";  // "low", "medium", "high", "ultra"
	std::vector<std::string> preload_list;
};

struct UIConfig {
	std::string font_path = "resources/fonts/default.ttf";
	int font_size = 14;
	float scale = 1.0f;
	std::string locale = "en_US";
	std::string theme = "dark";
	std::string color_blind_mode = "none";  // "none", "protanopia", "deuteranopia", "tritanopia"
};

struct PlatformConfig {
	std::string save_data_path;
	std::string cache_path;
	std::string locale;
};

// ── Runtime / Client / Server boundary ───────────────────────────────────
//
// RuntimeConfig  — shared by client and server builds.
//                  log, frame, resource_dir, sandbox_level, scripts_dir,
//                  environment.
//
// ClientConfig   — client-only. render, window, input, audio, network,
//                  assets, ui, platform, scripts_dir.
//
// ServerConfig   — server-only. admin, mongodb, db_service, msgpack, http,
//                  resource_limits, instance, tcp_keepalive.

// Client config — loaded from resources/config/client/client.json (Layer 2),
// with user overrides from <user_data>/settings.json (Layer 3).
struct ClientConfig {
	// Shared with RuntimeConfig (per-build defaults may differ)
	std::string scripts_dir = config::kDefaultClientScriptsDir;

	// Client-only subsystems
	RenderConfig render;
	WindowConfig window;
	InputConfig input;
	AudioConfig audio;
	NetworkClientConfig network;
	AssetConfig assets;
	UIConfig ui;
	PlatformConfig platform;

	// Set to true after first successful launch. Used to detect first-run
	// experience (e.g. show welcome dialog, create default user settings).
	bool first_run_completed = false;
};

struct HttpConfig {
	double timeout_sec = config::kDefaultHttpTimeoutSec;
};

struct MsgpackConfig {
	int max_nesting_depth = config::kDefaultMsgpackMaxNestingDepth;
	size_t max_payload_size = config::kDefaultMsgpackMaxPayloadSize;
};

// MongoDB cluster config — loaded from the path referenced by
// server.json's "mongodb_dev" / "mongodb_public" fields.
//
// C++ members use snake_case. JSON keys remain camelCase;
// the mapping is handled by glz::meta specializations below.

struct MongoDbConnectionOptions {
	std::string read_preference = "primaryPreferred";
	int max_pool_size = 10;
	int connect_timeout_ms = 5000;
	int server_selection_timeout_ms = 5000;
	int socket_timeout_ms = 30000;
};

struct MongoDbConnectionConfig {
	std::string uri;
	std::vector<std::string> hosts;
	std::string replica_set;
	bool direct_connection = false;
	MongoDbConnectionOptions options;
};

struct MongoDbClusterInfo {
	std::string type;
	std::string name;
	std::string version;
	std::string shell;
};

struct MongoDbNodeConfig {
	int id = 0;
	std::string name;
	std::string host;
	int port = 27017;
	int priority = 0;
	std::string role;
	std::string config;
	std::string db_path;
	std::string log_path;
	std::string pid_file;
};

struct MongoDbSecurityConfig {
	bool authentication = false;
	bool tls = false;
	std::string bind_ip = "127.0.0.1";
	std::string note;
};

struct MongoDbStorageConfig {
	std::string engine = "wiredTiger";
	bool directory_per_db = true;
	std::string compression = "snappy";
	bool journal = true;
};

struct MongoDbScriptsConfig {
	std::string start;
	std::string stop;
	std::string status;
	std::string init;
	std::string shell;
};

struct MongoDbDriverInfo {
	std::string package;
	std::string version;
	std::string test_program;
	std::string example;
};

struct MongoDbDriversConfig {
	MongoDbDriverInfo cpp;
	MongoDbDriverInfo python;
	MongoDbDriverInfo nodejs;
	MongoDbDriverInfo go;
	MongoDbDriverInfo java;
};

// Full MongoDB cluster configuration.
struct MongoDbConfig {
	std::string _description;  // maps to "_description" JSON key
	std::string _updated;  // maps to "_updated" JSON key
	MongoDbClusterInfo cluster;
	MongoDbConnectionConfig connection;
	std::vector<MongoDbNodeConfig> nodes;
	MongoDbSecurityConfig security;
	MongoDbStorageConfig storage;
	MongoDbScriptsConfig scripts;
	MongoDbDriversConfig drivers;
};

// Server config — loaded from resources/config/server/server.json.
struct ServerConfig {
	HttpConfig http;
	MsgpackConfig msgpack;
	std::string scripts_dir = config::kDefaultServerScriptsDir;

	int admin_port = 8081;  // admin HTTP port; 0 = disabled
	std::string admin_bind_address = "127.0.0.1";  // bind for admin HTTP
	std::string config_webhook_url;                 // POST after Reload (empty=off)
	int config_webhook_timeout_sec = 5;

	// MongoDB cluster config file paths (relative to working dir).
	// These are separate JSON files with full cluster topology details.
	std::string mongodb_dev;  // development / local cluster
	std::string mongodb_public;	 // public / production cluster

	// Database service config file path (relative to working dir).
	std::string db_service = "resources/config/server/db_service.json";

	// Explicit MongoDB selection override. When non-empty, this forces
	// which MongoDB cluster to use ("dev" or "public"), overriding the
	// environment-based default. Empty (default) = use environment.
	std::string active_mongodb;  // "dev", "public", or "" (auto)

	// ── Server operations (Task 8) ──────────────────────────────────

	// Graceful shutdown timeout in seconds. Cleanup phases that exceed
	// this limit are force-terminated.
	int shutdown_timeout_sec = config::kDefaultShutdownTimeoutSec;

	// Connection drain timeout in seconds. After stop-accepting,
	// existing connections get this window to finish in-flight work.
	int connection_drain_timeout_sec = config::kDefaultConnectionDrainTimeoutSec;

	// Maximum concurrent TCP connections (0 = unlimited).
	int max_connections = config::kDefaultMaxConnections;

	// PID file path (relative to working dir). Empty = no PID file.
	std::string pid_file = config::kDefaultPidFile;

	// TCP keepalive parameters. Zero values mean "use OS default".
	TcpKeepaliveConfig tcp_keepalive;

	// Resource limits — runtime-configurable, overriding compile-time defaults.
	ResourceLimits resource_limits;

	// Instance identity for multi-instance / cluster deployments.
	InstanceIdentity instance;
};

// ConfigManager — loads configs from JSON files at startup

class ENGINE_API ConfigManager : public IConfigManager {
	public:
	static ConfigManager& Instance();

	// Factory: creates an independent (non-singleton) ConfigManager.
	static std::unique_ptr<IConfigManager> Create();

	/* Callback type for config reload notifications.
	 * Callbacks MUST NOT throw — exceptions are caught and logged. */
	using ReloadCallback = std::function<void(const ConfigChangeSet&)>;

	ConfigManager(const ConfigManager&) = delete;
	ConfigManager& operator=(const ConfigManager&) = delete;

	// Destructor defined in config.cc where FileWatcher is complete.
	// Required because ENGINE_API forces destructor generation in every TU
	// that includes this header, and unique_ptr<FileWatcher> needs the
	// complete type.
	~ConfigManager();

	// ── Reload notification ──────────────────────────────────────────

	/* Register a callback to be invoked after each successful Reload().
	 * Returns a registration ID for later unregistration.
	 * Callbacks are invoked in registration order. */
	int RegisterReloadCallback(ReloadCallback callback);

	/* Unregister a previously registered callback by ID. */
	void UnregisterReloadCallback(int id);

	// ── Rollback ─────────────────────────────────────────────────────

	/* Rollback to the configuration snapshot saved before the last
	 * successful Reload(). Returns false if no snapshot is available.
	 * Notifies reload callbacks with a ConfigChangeSet describing the
	 * reverted changes. */
	bool Rollback();

	/* True if a previous-config snapshot is available for rollback. */
	bool CanRollback() const;

	// ── Auto-reload (FileWatcher) ────────────────────────────────────

	/* Enable automatic config reload when .json files change in the
	 * config directory tree. The watcher runs on a background thread;
	 * reload + callbacks are dispatched to the event loop if one is set.
	 * Not thread-safe — call from the main thread during Init(). */
	void EnableAutoReload(const std::string& config_dir);

	/* Disable auto-reload and stop the watcher thread. */
	void DisableAutoReload();

	/* True if the config FileWatcher is active. */
	bool IsAutoReloadEnabled() const;

	// ── From JSON strings (text) ─────────────────────────────────────

	bool LoadRuntimeFromString(const std::string& json);
	bool LoadClientFromString(const std::string& json);
	bool LoadServerFromString(const std::string& json);

	// ── From files ───────────────────────────────────────────────────

	bool LoadRuntimeFromFile(const std::string& path);
	bool LoadClientFromFile(const std::string& path);
	bool LoadServerFromFile(const std::string& path);

	// ── Client user settings (3-layer loading) ───────────────────────

	// Layer 3: load user overrides from <user_data>/settings.json.
	// Overwrites only fields present in the JSON; others keep their
	// Layer 2 (factory) values. Returns false on parse failure, which
	// triggers corruption recovery (rename corrupt file, keep Layer 2).
	bool LoadClientUserSettings(const std::string& user_settings_path);

	// Persist current ClientConfig to <user_data>/settings.json.
	// Writes the full config (not just overrides) to simplify the
	// on-disk format. Returns false on I/O failure (filesystem full, etc.).
	bool SaveClientUserSettings(const std::string& user_settings_path) const;

	// Full layered load: Layer 1 (C++ defaults) → Layer 2 (factory JSON)
	// → Layer 3 (user settings JSON).  On corruption of Layer 3, the
	// corrupt file is renamed to *.corrupt and loading continues with
	// Layer 2 only.
	bool LoadClientLayered(const std::string& factory_path,
	                       const std::string& user_settings_path);

	// Reset user settings: delete settings.json and reset ClientConfig
	// to factory defaults (Layer 2 only).
	bool ResetClientUserSettings(const std::string& user_settings_path);

	// Set/get the active deployment environment. Must be called
	// BEFORE Load() for profile layering to take effect.
	// Default is development. Overridden by --env CLI / EVPP_ENV.
	void SetActiveEnvironment(Environment env) { active_environment_ = env; }
	Environment GetActiveEnvironment() const { return active_environment_; }

	// Load all configs from a directory tree:
	//   {config_dir}/runtime/runtime.json
	//   {config_dir}/profiles/{env}.json       (profile overlay, optional)
	//   {config_dir}/client/client.json        (optional, Layer 2)
	//   <user_data>/settings.json              (optional, Layer 3 override)
	//   {config_dir}/server/server.json        (optional)
	bool Load(const std::string& config_dir);

	// ── Reload ───────────────────────────────────────────────────────

	// Reload configs from disk. Returns false on failure (current values
	// are preserved).
	bool Reload(const std::string& config_dir);

	// Validate — validate the currently loaded in-memory config.
	ValidationResult Validate() const override;

	// ValidateOnly — parse + validate files without applying changes.
	// Returns the validation result. Does not modify current config.
	ValidationResult ValidateOnly(const std::string& config_dir);

	// Dump — serialize current config to JSON for snapshot/audit.
	std::string Dump() const override;
	std::string DumpRuntime() const;
	std::string DumpServer() const;

	// Env-var interpolation — replaces ${VAR} and ${VAR:-default} in strings.
	// Called automatically during Load/Reload for all string fields.
	static std::string InterpolateEnvVars(const std::string& value);

	// ── Runtime / Client / Server accessors ──────────────────────────

	RuntimeConfig GetRuntimeConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return runtime_config_;
	}

	ClientConfig GetClientConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return client_config_;
	}

	ServerConfig GetServerConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return server_config_;
	}

	// SetRuntimeOverride — thread-safe setter for CLI/init-time overrides.
	// Must be called before Engine::Init() (single-threaded context).
	// After Init(), use Reload() instead.
	void SetRuntimeOverride(const RuntimeConfig& config) {
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		runtime_config_ = config;
	}
	void SetServerOverride(const ServerConfig& config) {
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		server_config_ = config;
	}

	// ── MongoDB config file paths (thread-safe) ──────────────────────

	// Returns the path configured in server.json, or empty string if unset.
	std::string GetMongoDbDevPath() const;
	std::string GetMongoDbPublicPath() const;

	// Override the path at runtime. Empty string clears the setting.
	void SetMongoDbDevPath(const std::string& path);
	void SetMongoDbPublicPath(const std::string& path);

	// True if the dev/public mongodb config path is set (non-empty).
	bool HasMongoDbDev() const;
	bool HasMongoDbPublic() const;

	// ── MongoDB cluster config loading ───────────────────────────────

	// Load a MongoDbConfig from a JSON file path.
	// Returns false on failure; out is untouched on failure.
	static bool LoadMongoDbConfigFromFile(const std::string& path, MongoDbConfig& out);

	// Load a DbServiceConfig from a JSON file path.
	static bool LoadDbServiceConfigFromFile(const std::string& path, DbServiceConfig& out);

	// Load the dev/public cluster config using the currently configured
	// path from server.json.
	bool LoadMongoDbDevConfig(MongoDbConfig& out) const;
	bool LoadMongoDbPublicConfig(MongoDbConfig& out) const;

	// ── Cached MongoDB config access (auto-loaded with server config) ─

	// Returns the cached dev/public mongodb config (thread-safe copy).
	// Valid only if the corresponding Has*() returns true and the file
	// was loaded successfully.
	MongoDbConfig GetMongoDbDevConfig() const;
	MongoDbConfig GetMongoDbPublicConfig() const;

	// True if the dev/public mongodb config was loaded (path was set and
	// the file was parsed without error).
	bool IsMongoDbDevLoaded() const;
	bool IsMongoDbPublicLoaded() const;

	// Reload the mongodb configs from the currently configured paths.
	// Returns true if all configured configs loaded successfully (or
	// none were configured). Called automatically by Load/Reload.
	bool ReloadMongoDbConfigs();

	// TOCTOU-safe compound access: holds lock through path read + file load.
	// Prefer these over GetMongoDbDevPath() + LoadMongoDbConfigFromFile().
	bool LoadMongoDbDevConfigLocked(MongoDbConfig& out) const;
	bool LoadMongoDbPublicConfigLocked(MongoDbConfig& out) const;

	// Build a field-level change set by diffing old and new configs.
	static ConfigChangeSet Diff(const RuntimeConfig& old_rt,
	                            const RuntimeConfig& new_rt,
	                            const ServerConfig& old_srv,
	                            const ServerConfig& new_srv);

	private:
	ConfigManager() = default;

	// Active deployment environment — set before Load(), reused by Reload().
	Environment active_environment_ = Environment::development;

	// Apply environment profile overlay on top of the currently loaded
	// runtime config. Reads profiles/{env}.json and merges matching fields.
	void ApplyProfileOverlay(const std::string& config_dir);

	// Shared loading helper: after server config is populated, load
	// any referenced mongodb config files.
	void LoadMongoDbConfigsFromServer();

	// Notify all registered reload callbacks with the given change set.
	void NotifyReloadCallbacks(const ConfigChangeSet& changes);

	mutable std::shared_mutex config_mutex_;
	RuntimeConfig runtime_config_;
	ClientConfig client_config_;
	ServerConfig server_config_;

	// Previous config snapshots for rollback support.
	RuntimeConfig previous_runtime_config_;
	ClientConfig previous_client_config_;
	ServerConfig previous_server_config_;
	bool has_previous_ = false;

	// Cached mongodb cluster configs — loaded alongside server.json.
	MongoDbConfig mongo_dev_config_;
	MongoDbConfig mongo_public_config_;
	bool mongo_dev_loaded_ = false;
	bool mongo_public_loaded_ = false;

	// Reload notification (plain mutex — callbacks are low-frequency)
	mutable std::mutex callbacks_mutex_;
	std::vector<std::pair<int, ReloadCallback>> callbacks_;
	int next_callback_id_ = 1;
	std::atomic<bool> reloading_{false};

	// Auto-reload: FileWatcher for .json config files.
	std::unique_ptr<FileWatcher> config_watcher_;
	std::string config_watcher_dir_;
};

}  // namespace engine

// ── glaze reflection metadata for MongoDB structs ──────────────────────────
// JSON keys use camelCase (matching existing mongodb config files);
// C++ members use snake_case (matching the rest of the codebase).

template <>
struct glz::meta<engine::MongoDbConnectionOptions> {
	using T = engine::MongoDbConnectionOptions;
	static constexpr auto value = glz::object(
		"readPreference", &T::read_preference,
		"maxPoolSize", &T::max_pool_size,
		"connectTimeoutMS", &T::connect_timeout_ms,
		"serverSelectionTimeoutMS", &T::server_selection_timeout_ms,
		"socketTimeoutMS", &T::socket_timeout_ms);
};

template <>
struct glz::meta<engine::MongoDbConnectionConfig> {
	using T = engine::MongoDbConnectionConfig;
	static constexpr auto value = glz::object(
		"uri", &T::uri,
		"hosts", &T::hosts,
		"replicaSet", &T::replica_set,
		"directConnection", &T::direct_connection,
		"options", &T::options);
};

template <>
struct glz::meta<engine::MongoDbClusterInfo> {
	using T = engine::MongoDbClusterInfo;
	static constexpr auto value = glz::object(
		"type", &T::type,
		"name", &T::name,
		"version", &T::version,
		"shell", &T::shell);
};

template <>
struct glz::meta<engine::MongoDbNodeConfig> {
	using T = engine::MongoDbNodeConfig;
	static constexpr auto value = glz::object(
		"id", &T::id,
		"name", &T::name,
		"host", &T::host,
		"port", &T::port,
		"priority", &T::priority,
		"role", &T::role,
		"config", &T::config,
		"dbPath", &T::db_path,
		"logPath", &T::log_path,
		"pidFile", &T::pid_file);
};

template <>
struct glz::meta<engine::MongoDbSecurityConfig> {
	using T = engine::MongoDbSecurityConfig;
	static constexpr auto value = glz::object(
		"authentication", &T::authentication,
		"tls", &T::tls,
		"bindIp", &T::bind_ip,
		"note", &T::note);
};

template <>
struct glz::meta<engine::MongoDbStorageConfig> {
	using T = engine::MongoDbStorageConfig;
	static constexpr auto value = glz::object(
		"engine", &T::engine,
		"directoryPerDB", &T::directory_per_db,
		"compression", &T::compression,
		"journal", &T::journal);
};

template <>
struct glz::meta<engine::MongoDbScriptsConfig> {
	using T = engine::MongoDbScriptsConfig;
	static constexpr auto value = glz::object(
		"start", &T::start,
		"stop", &T::stop,
		"status", &T::status,
		"init", &T::init,
		"shell", &T::shell);
};

template <>
struct glz::meta<engine::MongoDbDriverInfo> {
	using T = engine::MongoDbDriverInfo;
	static constexpr auto value = glz::object(
		"package", &T::package,
		"version", &T::version,
		"testProgram", &T::test_program,
		"example", &T::example);
};

template <>
struct glz::meta<engine::MongoDbDriversConfig> {
	using T = engine::MongoDbDriversConfig;
	static constexpr auto value = glz::object(
		"cpp", &T::cpp,
		"python", &T::python,
		"nodejs", &T::nodejs,
		"go", &T::go,
		"java", &T::java);
};

template <>
struct glz::meta<engine::MongoDbConfig> {
	using T = engine::MongoDbConfig;
	static constexpr auto value = glz::object(
		"_description", &T::_description,
		"_updated", &T::_updated,
		"cluster", &T::cluster,
		"connection", &T::connection,
		"nodes", &T::nodes,
		"security", &T::security,
		"storage", &T::storage,
		"scripts", &T::scripts,
		"drivers", &T::drivers);
};
