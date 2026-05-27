#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "runtime/config/config_constants.h"
#include "runtime/core/engine_api.h"

namespace engine {

// Forward declaration for database service config
struct DbServiceConfig;

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

//============================================================================
// MongoDB cluster config — loaded from the path referenced by
// server.json's "mongodb_dev" / "mongodb_public" fields.
//
// NOTE: Member names use camelCase to match the source JSON keys directly
// (glaze auto-reflection matches member names to JSON keys). This differs
// from the rest of the config structs because the mongodb config files
// were authored in camelCase.
//============================================================================

struct MongoDbConnectionOptions {
	std::string readPreference = "primaryPreferred";
	int maxPoolSize = 10;
	int connectTimeoutMS = 5000;
	int serverSelectionTimeoutMS = 5000;
	int socketTimeoutMS = 30000;
};

struct MongoDbConnectionConfig {
	std::string uri;
	std::vector<std::string> hosts;
	std::string replicaSet;
	bool directConnection = false;
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
	std::string dbPath;
	std::string logPath;
	std::string pidFile;
};

struct MongoDbSecurityConfig {
	bool authentication = false;
	bool tls = false;
	std::string bindIp = "127.0.0.1";
	std::string note;
};

struct MongoDbStorageConfig {
	std::string engine = "wiredTiger";
	bool directoryPerDB = true;
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
	std::string testProgram;
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

	// MongoDB cluster config file paths (relative to working dir).
	// These are separate JSON files with full cluster topology details.
	std::string mongodb_dev;  // development / local cluster
	std::string mongodb_public;	 // public / production cluster

	// Database service config file path (relative to working dir).
	std::string db_service = "resources/config/server/db_service.json";
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

	// ── Runtime / Client / Server accessors ──────────────────────────

	RuntimeConfig GetRuntimeConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return runtime_config_;
	}
	RuntimeConfig& GetRuntimeConfigMutable() {
		return runtime_config_;
	}

	ClientConfig GetClientConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return client_config_;
	}
	ClientConfig& GetClientConfigMutable() {
		return client_config_;
	}

	ServerConfig GetServerConfig() const {
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		return server_config_;
	}
	ServerConfig& GetServerConfigMutable() {
		return server_config_;
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

	// Returns the cached dev/public mongodb config. Valid only if the
	// corresponding Has*() returns true and the file was loaded successfully.
	const MongoDbConfig& GetMongoDbDevConfig() const;
	const MongoDbConfig& GetMongoDbPublicConfig() const;

	// True if the dev/public mongodb config was loaded (path was set and
	// the file was parsed without error).
	bool IsMongoDbDevLoaded() const;
	bool IsMongoDbPublicLoaded() const;

	// Reload the mongodb configs from the currently configured paths.
	// Returns true if all configured configs loaded successfully (or
	// none were configured). Called automatically by Load/Reload.
	bool ReloadMongoDbConfigs();

	private:
	ConfigManager() = default;

	// Shared loading helper: after server config is populated, load
	// any referenced mongodb config files.
	void LoadMongoDbConfigsFromServer();

	mutable std::shared_mutex config_mutex_;
	RuntimeConfig runtime_config_;
	ClientConfig client_config_;
	ServerConfig server_config_;

	// Cached mongodb cluster configs — loaded alongside server.json.
	MongoDbConfig mongo_dev_config_;
	MongoDbConfig mongo_public_config_;
	bool mongo_dev_loaded_ = false;
	bool mongo_public_loaded_ = false;
};

}  // namespace engine
