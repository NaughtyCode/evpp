#include "runtime/config/config.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
#include <type_traits>

#include <glaze/glaze.hpp>

#include "runtime/config/config_constants.h"
#include "runtime/config/config_validator.h"
#include "runtime/config/platform_paths.h"
#include "runtime/core/log/log.h"
#include "runtime/core/mem/mem.h"
#include "runtime/database/data_service/db_service_config.h"
#include "runtime/vm/file_watcher.h"

namespace engine {

namespace {

void CheckPlaintextCredentials(const std::string& uri, const std::string& context) {
	if (uri.find("://") != std::string::npos &&
		uri.find('@') != std::string::npos) {
		auto at_pos = uri.find('@');
		auto colon_pos = uri.rfind(':', at_pos);
		if (colon_pos != std::string::npos && colon_pos > uri.find("://") + 3) {
			if (auto* l = GetLogger())
				ENGINE_LOG_WARN(l,
					"ConfigManager: {} URI may contain embedded credentials. "
					"Use ${{ENV_VAR}} interpolation instead.", context);
		}
	}
}

bool IsRedisRuntimeCompiled() {
#if defined(ENGINE_REDIS_ENABLED)
	return true;
#else
	return false;
#endif
}

void WarnRedisPasswordConfigured(const RedisClientConfig& config) {
	if (config.connection.password.empty()) return;
	if (auto* l = GetLogger()) {
		ENGINE_LOG_WARN(l,
			"ConfigManager: redis password is configured and will be redacted in logs");
	}
}

void AppendRedisConfigDiff(ConfigChangeSet& changes,
						   const RedisClientConfig& old_config,
						   bool old_loaded,
						   const RedisClientConfig& new_config,
						   bool new_loaded);

// Recursively interpolate ${VAR} and ${VAR:-default} in all string fields of a
// glaze-reflectable config struct. Called automatically after every Load/Reload.
template <typename T>
void InterpolateConfigStrings(T& config) {
	auto* logger = GetLogger();
	(void)logger;
	glz::for_each_field(config, [&](auto& member) {
		using MemberType = std::decay_t<decltype(member)>;
		if constexpr (std::is_same_v<MemberType, std::string>) {
			member = ConfigManager::InterpolateEnvVars(member);
		} else if constexpr (std::is_same_v<MemberType, std::vector<std::string>>) {
			for (auto& s : member) {
				s = ConfigManager::InterpolateEnvVars(s);
			}
		}
		// Recursively interpolate nested structs
		if constexpr (glz::reflectable<MemberType> || glz::glaze_object_t<MemberType>) {
			InterpolateConfigStrings(member);
		}
	});
}

}  // namespace

ConfigManager::~ConfigManager() = default;

ConfigManager& ConfigManager::Instance() {
	static ConfigManager instance;
	return instance;
}

std::unique_ptr<IConfigManager> ConfigManager::Create() {
	return std::unique_ptr<IConfigManager>(CLOUDENGINE_MEM_NEW(ConfigManager));
}

// From JSON strings (text)

bool ConfigManager::LoadRuntimeFromString(const std::string& json) {
	RuntimeConfig temp;
	RuntimeConfig old_runtime;
	ServerConfig old_server;
	RuntimeConfig new_runtime;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		old_runtime = runtime_config_;
		old_server = server_config_;
	}
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse runtime config: {}", glz::format_error(ec, json));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::Validate(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: runtime config validation failed: {}", vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_runtime_config_ = runtime_config_;
		runtime_config_ = std::move(temp);
		has_previous_ = true;
		new_runtime = runtime_config_;
	}
	NotifyReloadCallbacks(Diff(old_runtime, new_runtime, old_server, old_server));
	return true;
}

bool ConfigManager::LoadClientFromString(const std::string& json) {
	ClientConfig temp;
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse client config: {}", glz::format_error(ec, json));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::ValidateClient(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: client config validation failed: {}", vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_client_config_ = std::move(client_config_);
		client_config_ = std::move(temp);
	}
	return true;
}

bool ConfigManager::ApplyClientOverridesFromString(const std::string& json) {
	ClientConfig merged;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		merged = client_config_;
	}

	auto ec = glz::read_json(merged, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse client override config: {}", glz::format_error(ec, json));
		return false;
	}
	InterpolateConfigStrings(merged);
	auto vr = ConfigValidator::ValidateClient(merged);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: client override config validation failed: {}", vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_client_config_ = client_config_;
		client_config_ = std::move(merged);
		has_previous_ = true;
	}
	return true;
}

bool ConfigManager::LoadServerFromString(const std::string& json) {
	ServerConfig temp;
	RuntimeConfig old_runtime;
	ServerConfig old_server;
	ServerConfig new_server;
	RedisClientConfig old_redis;
	bool old_redis_loaded = false;
	RedisClientConfig current_redis;
	bool current_redis_loaded = false;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		old_runtime = runtime_config_;
		old_server = server_config_;
		old_redis = redis_config_;
		old_redis_loaded = redis_loaded_;
	}
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse server config: {}", glz::format_error(ec, json));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::ValidateServer(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: server config validation failed: {}", vr.errors);
		return false;
	}

	MongoDbConfig new_mongo_dev;
	MongoDbConfig new_mongo_public;
	bool dev_loaded = false;
	bool public_loaded = false;
	if (!temp.mongodb_dev.empty()) {
		dev_loaded = LoadMongoDbConfigFromFile(temp.mongodb_dev, new_mongo_dev);
		if (!dev_loaded) return false;
		CheckPlaintextCredentials(new_mongo_dev.connection.uri, "mongodb_dev");
	}
	if (!temp.mongodb_public.empty()) {
		public_loaded = LoadMongoDbConfigFromFile(temp.mongodb_public, new_mongo_public);
		if (!public_loaded) return false;
		CheckPlaintextCredentials(new_mongo_public.connection.uri, "mongodb_public");
	}
	RedisClientConfig new_redis;
	bool redis_loaded = false;
	if (!LoadRedisConfigForServer(temp, new_redis, redis_loaded)) {
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_server_config_ = server_config_;
		previous_redis_config_ = redis_config_;
		previous_redis_loaded_ = redis_loaded_;
		server_config_ = std::move(temp);
		mongo_dev_config_ = std::move(new_mongo_dev);
		mongo_public_config_ = std::move(new_mongo_public);
		mongo_dev_loaded_ = dev_loaded;
		mongo_public_loaded_ = public_loaded;
		redis_config_ = std::move(new_redis);
		redis_loaded_ = redis_loaded;
		has_previous_ = true;
		new_server = server_config_;
		current_redis = redis_config_;
		current_redis_loaded = redis_loaded_;
	}
	auto changes = Diff(old_runtime, old_runtime, old_server, new_server);
	AppendRedisConfigDiff(changes, old_redis, old_redis_loaded,
						  current_redis, current_redis_loaded);
	NotifyReloadCallbacks(changes);
	return true;
}

// From files

bool ConfigManager::LoadRuntimeFromFile(const std::string& path) {
	RuntimeConfig temp;
	std::string buf;
	auto ec = glz::read_file_json(temp, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::Validate(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: runtime config validation failed [{}]: {}", path, vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_runtime_config_ = runtime_config_;
		runtime_config_ = std::move(temp);
		has_previous_ = true;
	}
	return true;
}

bool ConfigManager::LoadClientFromFile(const std::string& path) {
	ClientConfig temp;
	std::string buf;
	auto ec = glz::read_file_json(temp, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::ValidateClient(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: client config validation failed [{}]: {}", path, vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_client_config_ = client_config_;
		client_config_ = std::move(temp);
		has_previous_ = true;
	}
	return true;
}

bool ConfigManager::LoadServerFromFile(const std::string& path) {
	ServerConfig temp;
	std::string buf;
	auto ec = glz::read_file_json(temp, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(temp);
	auto vr = ConfigValidator::ValidateServer(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: server config validation failed [{}]: {}", path, vr.errors);
		return false;
	}

	MongoDbConfig new_mongo_dev;
	MongoDbConfig new_mongo_public;
	bool dev_loaded = false;
	bool public_loaded = false;
	if (!temp.mongodb_dev.empty()) {
		dev_loaded = LoadMongoDbConfigFromFile(temp.mongodb_dev, new_mongo_dev);
		if (!dev_loaded) return false;
		CheckPlaintextCredentials(new_mongo_dev.connection.uri, "mongodb_dev");
	}
	if (!temp.mongodb_public.empty()) {
		public_loaded = LoadMongoDbConfigFromFile(temp.mongodb_public, new_mongo_public);
		if (!public_loaded) return false;
		CheckPlaintextCredentials(new_mongo_public.connection.uri, "mongodb_public");
	}
	RedisClientConfig new_redis;
	bool redis_loaded = false;
	if (!LoadRedisConfigForServer(temp, new_redis, redis_loaded)) {
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_server_config_ = server_config_;
		previous_redis_config_ = redis_config_;
		previous_redis_loaded_ = redis_loaded_;
		server_config_ = std::move(temp);
		mongo_dev_config_ = std::move(new_mongo_dev);
		mongo_public_config_ = std::move(new_mongo_public);
		mongo_dev_loaded_ = dev_loaded;
		mongo_public_loaded_ = public_loaded;
		redis_config_ = std::move(new_redis);
		redis_loaded_ = redis_loaded;
		has_previous_ = true;
	}
	return true;
}

// ── Client user settings (3-layer loading) ──────────────────────────────

bool ConfigManager::LoadClientUserSettings(const std::string& user_settings_path) {
	std::error_code ec;
	if (!std::filesystem::exists(user_settings_path, ec)) {
		return true;
	}

	ClientConfig merged;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		merged = client_config_;
	}

	std::string buf;
	auto err = glz::read_file_json(merged, user_settings_path, buf);
	if (err) {
		if (auto* l = GetLogger())
			ENGINE_LOG_WARN(l,
				"ConfigManager: user settings [{}] are corrupt ({}). "
				"Renaming to .corrupt and continuing with factory defaults.",
				user_settings_path, glz::format_error(err, buf));

		std::error_code rename_ec;
		std::filesystem::rename(user_settings_path, user_settings_path + ".corrupt", rename_ec);
		if (rename_ec) {
			if (auto* l = GetLogger())
				ENGINE_LOG_ERROR(l,
					"ConfigManager: failed to rename corrupt settings [{}]: {}",
					user_settings_path, rename_ec.message());
		}
		return true;
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		client_config_ = std::move(merged);
	}

	if (auto* l = GetLogger())
		ENGINE_LOG_INFO(l, "ConfigManager: loaded user settings [{}]", user_settings_path);
	return true;
}

bool ConfigManager::SaveClientUserSettings(const std::string& user_settings_path) const {
	std::filesystem::path file_path(user_settings_path);
	if (!file_path.parent_path().empty()) {
		std::error_code ec;
		std::filesystem::create_directories(file_path.parent_path(), ec);
		if (ec) {
			if (auto* l = GetLogger())
				ENGINE_LOG_ERROR(l,
					"ConfigManager: failed to create user settings directory [{}]: {}",
					file_path.parent_path().string(), ec.message());
			return false;
		}
	}

	ClientConfig current;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		current = client_config_;
	}

	auto json_result = glz::write_json(current);
	if (!json_result) {
		if (auto* l = GetLogger())
			ENGINE_LOG_ERROR(l,
				"ConfigManager: failed to serialize user settings [{}]: {}",
				user_settings_path,
				glz::format_error(json_result.error()));
		return false;
	}
	std::string json = std::move(*json_result);

	// Write to a temporary file first, then rename for atomicity.
	std::string tmp_path = user_settings_path + ".tmp";
	{
		std::ofstream ofs(tmp_path, std::ios::out | std::ios::trunc);
		if (!ofs) {
			if (auto* l = GetLogger())
				ENGINE_LOG_ERROR(l,
					"ConfigManager: failed to open user settings [{}] for writing",
					user_settings_path);
			return false;
		}
		ofs << json;
		if (!ofs) {
			ofs.close();
			std::filesystem::remove(tmp_path);
			return false;
		}
	}
	std::error_code rename_ec;
	std::filesystem::remove(user_settings_path, rename_ec);
	rename_ec.clear();
	std::filesystem::rename(tmp_path, user_settings_path, rename_ec);
	if (rename_ec) {
		std::filesystem::remove(tmp_path);
		return false;
	}

	if (auto* l = GetLogger())
		ENGINE_LOG_INFO(l, "ConfigManager: saved user settings [{}]", user_settings_path);
	return true;
}

bool ConfigManager::LoadClientLayered(const std::string& factory_path,
                                       const std::string& user_settings_path) {
	ClientConfig fresh;
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		client_config_ = fresh;
	}

	if (!LoadClientFromFile(factory_path)) {
		return false;
	}

	LoadClientUserSettings(user_settings_path);

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		client_config_.first_run_completed = true;
	}

	return true;
}

bool ConfigManager::ResetClientUserSettings(const std::string& user_settings_path) {
	std::error_code ec;
	std::filesystem::remove(user_settings_path, ec);

	ClientConfig fresh;
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		client_config_ = std::move(fresh);
	}

	if (auto* l = GetLogger())
		ENGINE_LOG_INFO(l, "ConfigManager: reset client user settings");
	return true;
}

bool ConfigManager::Load(const std::string& config_dir) {
	using namespace config;

	// Runtime config is required — both client and server need it.
	std::string runtime_path = config_dir + kRuntimeConfigFile;
	if (!LoadRuntimeFromFile(runtime_path)) return false;

	// Apply environment profile overlay (common.json → {env}.json).
	// The overlay JSON contains only the fields to override for this env.
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		runtime_config_.environment = EnvironmentToString(GetActiveEnvironment());
	}
	if (!ApplyProfileOverlay(config_dir)) {
		return false;
	}

	// Client and server configs are optional — one may not exist
	// depending on the build target.  Check existence first to avoid
	// spurious "failed to load" messages on stderr.
	std::error_code ec;
	bool have_client = false;
	bool have_server = false;
	std::string client_path = config_dir + kClientConfigFile;
	if (std::filesystem::exists(client_path, ec)) {
		have_client = LoadClientFromFile(client_path);
		if (!have_client) {
			return false;
		}
		// Layer 3: load user overrides from <user_data>/settings.json.
		// Best-effort — failure doesn't invalidate the config.
		if (have_client) {
			std::string user_settings = platform::GetUserSettingsPath(kDefaultWindowTitle);
			LoadClientUserSettings(user_settings);
			{
				std::lock_guard<std::shared_mutex> lock(config_mutex_);
				client_config_.first_run_completed = true;
			}
		}
	}
	std::string server_path = config_dir + kServerConfigFile;
	if (std::filesystem::exists(server_path, ec)) {
		have_server = LoadServerFromFile(server_path);
		if (!have_server) {
			return false;
		}
	}

	// Cross-field validation after all configs are loaded.
	{
		auto vr = ConfigValidator::ValidateCross(runtime_config_, server_config_);
		if (!vr.valid) {
			if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: cross-field validation failed: {}", vr.errors);
			return false;
		}
	}
	return true;
}

bool ConfigManager::ApplyProfileOverlay(const std::string& config_dir) {
	std::string env_str = EnvironmentToString(GetActiveEnvironment());
	std::string profile_path = config_dir + "/profiles/" + env_str + ".json";

	std::error_code ec;
	if (!std::filesystem::exists(profile_path, ec)) {
		// No profile file for this environment — that's fine, use base config.
		return true;
	}

	// Read profile JSON (partial config — only overrides relevant fields).
	RuntimeConfig profile_overlay;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		profile_overlay = runtime_config_;
	}

	std::string buf;
	auto err = glz::read_file_json(profile_overlay, profile_path, buf);
	if (err) {
		if (auto* l = GetLogger())
			ENGINE_LOG_ERROR(l, "ConfigManager: failed to load profile [{}]: {}",
							 profile_path, glz::format_error(err, buf));
		return false;
	}

	// Restore the environment field (profile overlay may have overwritten it).
	profile_overlay.environment = env_str;
	InterpolateConfigStrings(profile_overlay);
	auto vr = ConfigValidator::Validate(profile_overlay);
	if (!vr.valid) {
		if (auto* l = GetLogger())
			ENGINE_LOG_ERROR(l, "ConfigManager: invalid profile [{}]: {}",
							 profile_path, vr.errors);
		return false;
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		runtime_config_ = std::move(profile_overlay);
	}

	if (auto* l = GetLogger())
		ENGINE_LOG_INFO(l, "ConfigManager: applied profile overlay [{}]", profile_path);
	return true;
}

// ── Diff helper ────────────────────────────────────────────────────────────

namespace {

template <typename T>
std::string ToString(const T& val) {
	if constexpr (std::is_same_v<T, std::string>) {
		return val;
	} else if constexpr (std::is_same_v<T, double>) {
		return std::to_string(val);
	} else if constexpr (std::is_same_v<T, bool>) {
		return val ? "true" : "false";
	} else if constexpr (std::is_integral_v<T>) {
		return std::to_string(val);
	} else {
		return "<complex>";
	}
}

void EmitChange(ConfigChangeSet& changes, const std::string& path,
				const std::string& old_val, const std::string& new_val) {
	if (old_val != new_val) {
		changes.push_back({path, old_val, new_val});
	}
}

void AppendRedisConfigDiff(ConfigChangeSet& changes,
						   const RedisClientConfig& old_config,
						   bool old_loaded,
						   const RedisClientConfig& new_config,
						   bool new_loaded) {
	EmitChange(changes, "redis.loaded", ToString(old_loaded), ToString(new_loaded));
	if (!old_loaded || !new_loaded) {
		return;
	}

	EmitChange(changes, "redis.connection.host",
			   old_config.connection.host, new_config.connection.host);
	EmitChange(changes, "redis.connection.port",
			   ToString(old_config.connection.port), ToString(new_config.connection.port));
	EmitChange(changes, "redis.connection.username",
			   old_config.connection.username.empty() ? "unset" : "set",
			   new_config.connection.username.empty() ? "unset" : "set");
	if (old_config.connection.password != new_config.connection.password) {
		changes.push_back({"redis.connection.password",
						   RedactedRedisPasswordForDiff(old_config.connection.password,
														old_config.connection.password),
						   RedactedRedisPasswordForDiff(old_config.connection.password,
														new_config.connection.password)});
	}
	EmitChange(changes, "redis.connection.database",
			   ToString(old_config.connection.database), ToString(new_config.connection.database));
	EmitChange(changes, "redis.connection.connect_timeout_ms",
			   ToString(old_config.connection.connect_timeout_ms),
			   ToString(new_config.connection.connect_timeout_ms));
	EmitChange(changes, "redis.connection.command_timeout_ms",
			   ToString(old_config.connection.command_timeout_ms),
			   ToString(new_config.connection.command_timeout_ms));
	EmitChange(changes, "redis.connection.keepalive",
			   ToString(old_config.connection.keepalive), ToString(new_config.connection.keepalive));
	EmitChange(changes, "redis.queue.request_queue_size",
			   ToString(old_config.queue.request_queue_size),
			   ToString(new_config.queue.request_queue_size));
	EmitChange(changes, "redis.queue.max_inflight",
			   ToString(old_config.queue.max_inflight), ToString(new_config.queue.max_inflight));
	EmitChange(changes, "redis.queue.dispatch_batch_size",
			   ToString(old_config.queue.dispatch_batch_size),
			   ToString(new_config.queue.dispatch_batch_size));
	EmitChange(changes, "redis.thread.thread_count",
			   ToString(old_config.thread.thread_count), ToString(new_config.thread.thread_count));
	EmitChange(changes, "redis.thread.main_loop_fps",
			   ToString(old_config.thread.main_loop_fps),
			   ToString(new_config.thread.main_loop_fps));
	EmitChange(changes, "redis.script.redis_scripts_dir",
			   old_config.script.redis_scripts_dir, new_config.script.redis_scripts_dir);
	EmitChange(changes, "redis.script.auto_load",
			   ToString(old_config.script.auto_load), ToString(new_config.script.auto_load));
	EmitChange(changes, "redis.log.enabled",
			   ToString(old_config.log.enabled), ToString(new_config.log.enabled));
	EmitChange(changes, "redis.log.slow_command_ms",
			   ToString(old_config.log.slow_command_ms),
			   ToString(new_config.log.slow_command_ms));
	EmitChange(changes, "redis.reconnect.enabled",
			   ToString(old_config.reconnect.enabled),
			   ToString(new_config.reconnect.enabled));
	EmitChange(changes, "redis.reconnect.initial_delay_ms",
			   ToString(old_config.reconnect.initial_delay_ms),
			   ToString(new_config.reconnect.initial_delay_ms));
	EmitChange(changes, "redis.reconnect.max_delay_ms",
			   ToString(old_config.reconnect.max_delay_ms),
			   ToString(new_config.reconnect.max_delay_ms));
	EmitChange(changes, "redis.reconnect.backoff_multiplier",
			   ToString(old_config.reconnect.backoff_multiplier),
			   ToString(new_config.reconnect.backoff_multiplier));
	EmitChange(changes, "redis.reconnect.queue_while_disconnected",
			   ToString(old_config.reconnect.queue_while_disconnected),
			   ToString(new_config.reconnect.queue_while_disconnected));
}

}  // namespace

ConfigChangeSet ConfigManager::Diff(const RuntimeConfig& old_rt,
									 const RuntimeConfig& new_rt,
									 const ServerConfig& old_srv,
									 const ServerConfig& new_srv) {
	ConfigChangeSet changes;

	// ── RuntimeConfig fields ──────────────────────────────────────────
	EmitChange(changes, "resource_dir", old_rt.resource_dir, new_rt.resource_dir);
	EmitChange(changes, "scripts_dir", old_rt.scripts_dir, new_rt.scripts_dir);
	EmitChange(changes, "sandbox_level", old_rt.sandbox_level, new_rt.sandbox_level);
	EmitChange(changes, "environment", old_rt.environment, new_rt.environment);

	// LogConfig
	EmitChange(changes, "log.dir", old_rt.log.dir, new_rt.log.dir);
	EmitChange(changes, "log.level", old_rt.log.level, new_rt.log.level);
	EmitChange(changes, "log.rotation_size_mb",
			   ToString(old_rt.log.rotation_size_mb), ToString(new_rt.log.rotation_size_mb));
	EmitChange(changes, "log.max_backup_files",
			   ToString(old_rt.log.max_backup_files), ToString(new_rt.log.max_backup_files));
	EmitChange(changes, "log.rotation_frequency", old_rt.log.rotation_frequency, new_rt.log.rotation_frequency);

	// FrameConfig
	EmitChange(changes, "frame.target_fps",
			   ToString(old_rt.frame.target_fps), ToString(new_rt.frame.target_fps));
	EmitChange(changes, "frame.interval_ms",
			   ToString(old_rt.frame.interval_ms), ToString(new_rt.frame.interval_ms));
	EmitChange(changes, "frame.slow_threshold_multiplier",
			   ToString(old_rt.frame.slow_threshold_multiplier),
			   ToString(new_rt.frame.slow_threshold_multiplier));

	// HotReloadConfig
	EmitChange(changes, "hot_reload.enabled",
			   ToString(old_rt.hot_reload.enabled), ToString(new_rt.hot_reload.enabled));
	EmitChange(changes, "hot_reload.startup_delay_ms",
			   ToString(old_rt.hot_reload.startup_delay_ms),
			   ToString(new_rt.hot_reload.startup_delay_ms));
	EmitChange(changes, "hot_reload.poll_interval_ms",
			   ToString(old_rt.hot_reload.poll_interval_ms),
			   ToString(new_rt.hot_reload.poll_interval_ms));
	EmitChange(changes, "hot_reload.debounce_ms",
			   ToString(old_rt.hot_reload.debounce_ms),
			   ToString(new_rt.hot_reload.debounce_ms));

	// ── ServerConfig fields ──────────────────────────────────────────
	EmitChange(changes, "http.timeout_sec",
			   ToString(old_srv.http.timeout_sec), ToString(new_srv.http.timeout_sec));
	EmitChange(changes, "msgpack.max_nesting_depth",
			   ToString(old_srv.msgpack.max_nesting_depth), ToString(new_srv.msgpack.max_nesting_depth));
	EmitChange(changes, "msgpack.max_payload_size",
			   ToString(old_srv.msgpack.max_payload_size), ToString(new_srv.msgpack.max_payload_size));
	EmitChange(changes, "server.scripts_dir", old_srv.scripts_dir, new_srv.scripts_dir);
	EmitChange(changes, "admin_port",
			   ToString(old_srv.admin_port), ToString(new_srv.admin_port));
	EmitChange(changes, "admin_bind_address", old_srv.admin_bind_address, new_srv.admin_bind_address);
	EmitChange(changes, "admin_auth_token",
			   old_srv.admin_auth_token.empty() ? "unset" : "set",
			   new_srv.admin_auth_token.empty() ? "unset" : "set");
	EmitChange(changes, "admin_metrics_enabled",
			   ToString(old_srv.admin_metrics_enabled), ToString(new_srv.admin_metrics_enabled));
	EmitChange(changes, "mongodb_dev", old_srv.mongodb_dev, new_srv.mongodb_dev);
	EmitChange(changes, "mongodb_public", old_srv.mongodb_public, new_srv.mongodb_public);
	EmitChange(changes, "active_mongodb", old_srv.active_mongodb, new_srv.active_mongodb);
	EmitChange(changes, "db_service", old_srv.db_service, new_srv.db_service);
	EmitChange(changes, "db_required",
			   ToString(old_srv.db_required), ToString(new_srv.db_required));
	EmitChange(changes, "server.redis", old_srv.redis, new_srv.redis);
	EmitChange(changes, "server.redis_required",
			   ToString(old_srv.redis_required), ToString(new_srv.redis_required));
	EmitChange(changes, "shutdown_timeout_sec",
			   ToString(old_srv.shutdown_timeout_sec), ToString(new_srv.shutdown_timeout_sec));
	EmitChange(changes, "connection_drain_timeout_sec",
			   ToString(old_srv.connection_drain_timeout_sec),
			   ToString(new_srv.connection_drain_timeout_sec));
	EmitChange(changes, "max_connections",
			   ToString(old_srv.max_connections), ToString(new_srv.max_connections));
	EmitChange(changes, "pid_file", old_srv.pid_file, new_srv.pid_file);
	EmitChange(changes, "resource_limits.max_message_size",
			   ToString(old_srv.resource_limits.max_message_size),
			   ToString(new_srv.resource_limits.max_message_size));
	EmitChange(changes, "resource_limits.max_buffer_capacity",
			   ToString(old_srv.resource_limits.max_buffer_capacity),
			   ToString(new_srv.resource_limits.max_buffer_capacity));
	EmitChange(changes, "resource_limits.max_http_body_size",
			   ToString(old_srv.resource_limits.max_http_body_size),
			   ToString(new_srv.resource_limits.max_http_body_size));
	EmitChange(changes, "resource_limits.max_msgpack_depth",
			   ToString(old_srv.resource_limits.max_msgpack_depth),
			   ToString(new_srv.resource_limits.max_msgpack_depth));
	EmitChange(changes, "instance.id", old_srv.instance.id, new_srv.instance.id);
	EmitChange(changes, "instance.region", old_srv.instance.region, new_srv.instance.region);
	EmitChange(changes, "instance.zone", old_srv.instance.zone, new_srv.instance.zone);
	EmitChange(changes, "instance.cluster", old_srv.instance.cluster, new_srv.instance.cluster);

	return changes;
}

// Reload (runtime — logger is available)

bool ConfigManager::Reload(const std::string& config_dir) {
	using namespace config;
	auto* logger = GetLogger();

	RuntimeConfig new_runtime;
	ClientConfig new_client;
	ServerConfig new_server;

	std::string buf;
	auto ec = glz::read_file_json(new_runtime, config_dir + kRuntimeConfigFile, buf);
	if (ec) {
		ENGINE_LOG_ERROR(logger,
						 "ConfigManager: reload failed for runtime.json: {}",
						 glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(new_runtime);
	{
		auto vr = ConfigValidator::Validate(new_runtime);
		if (!vr.valid) {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload validation failed for runtime.json: {}",
							 vr.errors);
			return false;
		}
	}

	// Apply environment profile overlay (common.json → {env}.json).
	Environment active_environment = GetActiveEnvironment();
	new_runtime.environment = EnvironmentToString(active_environment);
	{
		std::string profile_path =
			config_dir + "/profiles/" + EnvironmentToString(active_environment) + ".json";
		std::error_code exists_ec;
		if (std::filesystem::exists(profile_path, exists_ec)) {
			RuntimeConfig profile_overlay = new_runtime;
			std::string buf2;
			auto err = glz::read_file_json(profile_overlay, profile_path, buf2);
			if (!err) {
				profile_overlay.environment = EnvironmentToString(active_environment);
				InterpolateConfigStrings(profile_overlay);
				auto vr = ConfigValidator::Validate(profile_overlay);
				if (!vr.valid) {
					ENGINE_LOG_ERROR(logger, "ConfigManager: reload invalid profile [{}]: {}",
									 profile_path, vr.errors);
					return false;
				}
				new_runtime = std::move(profile_overlay);
				ENGINE_LOG_INFO(logger, "ConfigManager: reload applied profile overlay [{}]", profile_path);
			} else {
				ENGINE_LOG_ERROR(logger, "ConfigManager: reload failed to load profile [{}]: {}",
								 profile_path, glz::format_error(err, buf2));
				return false;
			}
		}
	}

	bool have_client = false;
	std::error_code ec2;
	std::string client_path = config_dir + kClientConfigFile;
	if (std::filesystem::exists(client_path, ec2)) {
		buf.clear();
		auto ec3 = glz::read_file_json(new_client, client_path, buf);
		if (!ec3) {
			InterpolateConfigStrings(new_client);
			auto vr = ConfigValidator::ValidateClient(new_client);
			if (!vr.valid) {
				ENGINE_LOG_ERROR(logger, "ConfigManager: reload validation failed for client.json: {}",
								 vr.errors);
				return false;
			}
			have_client = true;
			// Layer 3: load user overrides on top of factory settings.
			std::string user_path = platform::GetUserSettingsPath(config::kDefaultWindowTitle);
			std::error_code uec;
			if (std::filesystem::exists(user_path, uec)) {
				std::string ubuf;
				auto uerr = glz::read_file_json(new_client, user_path, ubuf);
				if (uerr) {
					if (auto* l = GetLogger())
						ENGINE_LOG_WARN(l,
							"ConfigManager: reload user settings [{}] parse error: {}",
							user_path, glz::format_error(uerr, ubuf));
				}
			}
			new_client.first_run_completed = true;
		} else {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload parse error for client.json: {}",
							 glz::format_error(ec3, buf));
			return false;
		}
	}

	bool have_server = false;
	std::string server_path = config_dir + kServerConfigFile;
	if (std::filesystem::exists(server_path, ec2)) {
		buf.clear();
		auto ec3 = glz::read_file_json(new_server, server_path, buf);
		if (!ec3) {
			InterpolateConfigStrings(new_server);
			have_server = true;
		} else {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload parse error for server.json: {}",
							 glz::format_error(ec3, buf));
			return false;
		}
	}
	if (have_server) {
		auto vr = ConfigValidator::ValidateServer(new_server);
		if (!vr.valid) {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload validation failed for server.json: {}",
							 vr.errors);
			return false;
		}
	}

	// Load mongodb configs from the new server config (before lock)
	MongoDbConfig new_mongo_dev;
	MongoDbConfig new_mongo_public;
	bool dev_loaded = false, public_loaded = false;
	RedisClientConfig new_redis;
	bool redis_loaded = false;
	if (have_server) {
		if (!new_server.mongodb_dev.empty()) {
			dev_loaded = LoadMongoDbConfigFromFile(new_server.mongodb_dev, new_mongo_dev);
			if (!dev_loaded) {
				ENGINE_LOG_ERROR(logger,
								 "ConfigManager: reload failed because mongodb_dev [{}] could not be loaded",
								 new_server.mongodb_dev);
				return false;
			}
		}
		if (!new_server.mongodb_public.empty()) {
			public_loaded = LoadMongoDbConfigFromFile(new_server.mongodb_public, new_mongo_public);
			if (!public_loaded) {
				ENGINE_LOG_ERROR(logger,
								 "ConfigManager: reload failed because mongodb_public [{}] could not be loaded",
								 new_server.mongodb_public);
				return false;
			}
		}
		if (!LoadRedisConfigForServer(new_server, new_redis, redis_loaded)) {
			ENGINE_LOG_ERROR(logger,
							 "ConfigManager: reload failed because redis config could not be loaded");
			return false;
		}
	}

	{
		ServerConfig current_server;
		if (!have_server) {
			std::shared_lock<std::shared_mutex> lock(config_mutex_);
			current_server = server_config_;
		}
		auto vr = ConfigValidator::ValidateCross(new_runtime, have_server ? new_server : current_server);
		if (!vr.valid) {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload cross-field validation failed: {}",
							 vr.errors);
			return false;
		}
	}

	// Snapshot old values before swap for diff + rollback.
	RuntimeConfig old_runtime;
	ClientConfig old_client;
	ServerConfig old_server;
	RedisClientConfig old_redis;
	bool old_redis_loaded = false;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		old_runtime = runtime_config_;
		old_client = client_config_;
		old_server = server_config_;
		old_redis = redis_config_;
		old_redis_loaded = redis_loaded_;
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		runtime_config_ = std::move(new_runtime);
		if (have_client) {
			client_config_ = std::move(new_client);
		}
		if (have_server) {
			server_config_ = std::move(new_server);
			mongo_dev_config_ = std::move(new_mongo_dev);
			mongo_public_config_ = std::move(new_mongo_public);
			mongo_dev_loaded_ = dev_loaded;
			mongo_public_loaded_ = public_loaded;
			redis_config_ = std::move(new_redis);
			redis_loaded_ = redis_loaded;
		}
	}

	// Save previous config for rollback.
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		previous_runtime_config_ = std::move(old_runtime);
		previous_client_config_ = std::move(old_client);
		previous_server_config_ = std::move(old_server);
		previous_redis_config_ = std::move(old_redis);
		previous_redis_loaded_ = old_redis_loaded;
		has_previous_ = true;
	}

	// Build field-level change set and log it.
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		ConfigChangeSet changes = Diff(previous_runtime_config_, runtime_config_,
									   previous_server_config_, server_config_);
		AppendRedisConfigDiff(changes, previous_redis_config_, previous_redis_loaded_,
							  redis_config_, redis_loaded_);
		for (const auto& entry : changes) {
			ENGINE_LOG_INFO(logger, "config: {}: {} -> {}",
							entry.field_path, entry.old_value, entry.new_value);
		}
		ENGINE_LOG_INFO(logger, "ConfigManager: config reloaded ({} fields changed)", changes.size());
		lock.unlock();
		NotifyReloadCallbacks(changes);
	}
	return true;
}

// MongoDB config file paths (thread-safe)

std::string ConfigManager::GetMongoDbDevPath() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return server_config_.mongodb_dev;
}

std::string ConfigManager::GetMongoDbPublicPath() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return server_config_.mongodb_public;
}

void ConfigManager::SetMongoDbDevPath(const std::string& path) {
	std::lock_guard<std::shared_mutex> lock(config_mutex_);
	server_config_.mongodb_dev = path;
}

void ConfigManager::SetMongoDbPublicPath(const std::string& path) {
	std::lock_guard<std::shared_mutex> lock(config_mutex_);
	server_config_.mongodb_public = path;
}

bool ConfigManager::HasMongoDbDev() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return !server_config_.mongodb_dev.empty();
}

bool ConfigManager::HasMongoDbPublic() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return !server_config_.mongodb_public.empty();
}

// MongoDB cluster config loading

bool ConfigManager::LoadMongoDbConfigFromFile(const std::string& path, MongoDbConfig& out) {
	std::string buf;
	auto ec = glz::read_file_json(out, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load mongodb config [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(out);
	return true;
}

bool ConfigManager::LoadDbServiceConfigFromFile(const std::string& path, DbServiceConfig& out) {
	std::string buf;
	auto ec = glz::read_file_json(out, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load db_service config [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	InterpolateConfigStrings(out);
	return true;
}

bool ConfigManager::LoadRedisClientConfigFromFile(const std::string& path,
												  RedisClientConfig& out) {
	std::string buf;
	auto ec = glz::read_file_json(out, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) {
			ENGINE_LOG_ERROR(l, "ConfigManager: failed to load redis config [{}]: {}",
							 path, glz::format_error(ec, buf));
		}
		return false;
	}
	InterpolateConfigStrings(out);
	auto vr = ValidateRedisClientConfig(out);
	if (!vr.valid) {
		if (auto* l = GetLogger()) {
			ENGINE_LOG_ERROR(l, "ConfigManager: redis config validation failed [{}]: {}",
							 path, vr.errors);
		}
		return false;
	}
	if (!vr.warnings.empty()) {
		if (auto* l = GetLogger()) {
			ENGINE_LOG_WARN(l, "ConfigManager: redis config validation warning [{}]: {}",
							path, vr.warnings);
		}
	}
	WarnRedisPasswordConfigured(out);
	return true;
}

bool ConfigManager::LoadRedisConfigForServer(const ServerConfig& server,
											 RedisClientConfig& out,
											 bool& loaded) const {
	out = RedisClientConfig{};
	loaded = false;

	if (!IsRedisRuntimeCompiled()) {
		if (server.redis_required) {
			if (auto* l = GetLogger()) {
				ENGINE_LOG_ERROR(l,
					"ConfigManager: redis_required=true but Redis runtime is not compiled");
			}
			return false;
		}
		return true;
	}

	if (server.redis.empty()) {
		if (server.redis_required) {
			if (auto* l = GetLogger()) {
				ENGINE_LOG_ERROR(l,
					"ConfigManager: redis_required=true but server.redis is empty");
			}
			return false;
		}
		return true;
	}

	RedisClientConfig candidate;
	if (!LoadRedisClientConfigFromFile(server.redis, candidate)) {
		if (server.redis_required) {
			return false;
		}
		if (auto* l = GetLogger()) {
			ENGINE_LOG_WARN(l,
				"ConfigManager: redis config [{}] failed to load; Redis disabled because redis_required=false",
				server.redis);
		}
		return true;
	}

	out = std::move(candidate);
	loaded = true;
	return true;
}

bool ConfigManager::LoadMongoDbDevConfig(MongoDbConfig& out) const {
	std::string path = GetMongoDbDevPath();
	if (path.empty()) return false;
	return LoadMongoDbConfigFromFile(path, out);
}

bool ConfigManager::LoadMongoDbPublicConfig(MongoDbConfig& out) const {
	std::string path = GetMongoDbPublicPath();
	if (path.empty()) return false;
	return LoadMongoDbConfigFromFile(path, out);
}

// TOCTOU-safe compound access: holds shared_lock through path read + file load.
bool ConfigManager::LoadMongoDbDevConfigLocked(MongoDbConfig& out) const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	if (server_config_.mongodb_dev.empty()) return false;
	return LoadMongoDbConfigFromFile(server_config_.mongodb_dev, out);
}

bool ConfigManager::LoadMongoDbPublicConfigLocked(MongoDbConfig& out) const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	if (server_config_.mongodb_public.empty()) return false;
	return LoadMongoDbConfigFromFile(server_config_.mongodb_public, out);
}

bool ConfigManager::LoadRedisClientConfigLocked(RedisClientConfig& out) const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	if (server_config_.redis.empty()) return false;
	return LoadRedisClientConfigFromFile(server_config_.redis, out);
}

// Cached MongoDB config access

MongoDbConfig ConfigManager::GetMongoDbDevConfig() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return mongo_dev_config_;
}

MongoDbConfig ConfigManager::GetMongoDbPublicConfig() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return mongo_public_config_;
}

bool ConfigManager::IsMongoDbDevLoaded() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return mongo_dev_loaded_;
}

bool ConfigManager::IsMongoDbPublicLoaded() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return mongo_public_loaded_;
}

RedisClientConfig ConfigManager::GetRedisClientConfig() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return redis_config_;
}

bool ConfigManager::IsRedisConfigLoaded() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return redis_loaded_;
}

bool ConfigManager::ReloadMongoDbConfigs() {
	std::string dev_path;
	std::string public_path;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		dev_path = server_config_.mongodb_dev;
		public_path = server_config_.mongodb_public;
	}

	MongoDbConfig new_dev;
	MongoDbConfig new_public;
	bool dev_ok = true, public_ok = true;

	if (!dev_path.empty()) {
		dev_ok = LoadMongoDbConfigFromFile(dev_path, new_dev);
	}
	if (!public_path.empty()) {
		public_ok = LoadMongoDbConfigFromFile(public_path, new_public);
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		if (!dev_path.empty() && dev_ok) {
			mongo_dev_config_ = std::move(new_dev);
			mongo_dev_loaded_ = true;
		} else if (!dev_path.empty()) {
			mongo_dev_loaded_ = false;
		}
		if (!public_path.empty() && public_ok) {
			mongo_public_config_ = std::move(new_public);
			mongo_public_loaded_ = true;
		} else if (!public_path.empty()) {
			mongo_public_loaded_ = false;
		}
	}

	return dev_ok && public_ok;
}

int ConfigManager::RegisterReloadCallback(ReloadCallback callback) {
	if (!callback) return 0;
	std::lock_guard<std::mutex> lock(callbacks_mutex_);
	int id = next_callback_id_++;
	callbacks_.emplace_back(id, std::move(callback));
	return id;
}

void ConfigManager::UnregisterReloadCallback(int id) {
	std::lock_guard<std::mutex> lock(callbacks_mutex_);
	callbacks_.erase(
		std::remove_if(callbacks_.begin(), callbacks_.end(),
					   [id](const auto& pair) { return pair.first == id; }),
		callbacks_.end());
}

void ConfigManager::NotifyReloadCallbacks(const ConfigChangeSet& changes) {
	if (reloading_.exchange(true)) return;  // prevent re-entrant reload

	std::vector<std::pair<int, ReloadCallback>> callbacks_copy;
	{
		std::lock_guard<std::mutex> lock(callbacks_mutex_);
		callbacks_copy = callbacks_;
	}
	auto* logger = GetLogger();
	for (auto& [id, callback] : callbacks_copy) {
		try {
			callback(changes);
		} catch (const std::exception& e) {
			ENGINE_LOG_ERROR(logger,
							 "ConfigManager: reload callback #{} failed: {}", id, e.what());
		} catch (...) {
			ENGINE_LOG_ERROR(logger,
							 "ConfigManager: reload callback #{} failed: unknown exception", id);
		}
	}

	reloading_.store(false);
}

// ── Rollback ─────────────────────────────────────────────────────────────

bool ConfigManager::Rollback() {
	RuntimeConfig old_runtime;
	ClientConfig old_client;
	ServerConfig old_server;
	RuntimeConfig reverted_runtime;
	ClientConfig reverted_client;
	ServerConfig reverted_server;
	RedisClientConfig old_redis;
	bool old_redis_loaded = false;
	RedisClientConfig reverted_redis;
	bool reverted_redis_loaded = false;
	bool has_snapshot = false;
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		if (!has_previous_) return false;

		old_runtime = runtime_config_;
		old_client = client_config_;
		old_server = server_config_;
		old_redis = redis_config_;
		old_redis_loaded = redis_loaded_;

		runtime_config_ = previous_runtime_config_;
		client_config_ = previous_client_config_;
		server_config_ = previous_server_config_;
		redis_config_ = previous_redis_config_;
		redis_loaded_ = previous_redis_loaded_;

		previous_runtime_config_ = old_runtime;
		previous_client_config_ = old_client;
		previous_server_config_ = old_server;
		previous_redis_config_ = old_redis;
		previous_redis_loaded_ = old_redis_loaded;
		has_snapshot = true;

		reverted_runtime = runtime_config_;
		reverted_client = client_config_;
		reverted_server = server_config_;
		reverted_redis = redis_config_;
		reverted_redis_loaded = redis_loaded_;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ConfigManager: rollback executed");

	// Build change set (swap old/new since we reverted)
	ConfigChangeSet changes = Diff(old_runtime, reverted_runtime,
								   old_server, reverted_server);
	AppendRedisConfigDiff(changes, old_redis, old_redis_loaded,
						  reverted_redis, reverted_redis_loaded);
	for (const auto& entry : changes) {
		ENGINE_LOG_INFO(logger, "config (rollback): {}: {} -> {}",
						entry.field_path, entry.old_value, entry.new_value);
	}

	NotifyReloadCallbacks(changes);
	return true;
}

bool ConfigManager::CanRollback() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return has_previous_;
}

// ── Auto-reload (FileWatcher) ──────────────────────────────────────────────

void ConfigManager::EnableAutoReload(const std::string& config_dir) {
	if (config_watcher_) {
		DisableAutoReload();
	}

	config_watcher_dir_ = config_dir;
	config_watcher_ = std::make_unique<FileWatcher>();
	config_watcher_->WatchDirectory(config_dir, ".json");
	config_watcher_->SetChangeCallback([this](const std::vector<std::string>& /*files*/) {
		// Reload on any .json change in the config tree.
		// Reload() is self-contained and thread-safe.
		if (!config_watcher_dir_.empty()) {
			Reload(config_watcher_dir_);
		}
	});
	config_watcher_->PrimeKnownFiles();
	config_watcher_->Start(1000);  // poll every 1s

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "ConfigManager: auto-reload enabled, watching [{}]", config_dir);
	}
}

void ConfigManager::DisableAutoReload() {
	if (config_watcher_) {
		config_watcher_->Stop();
		config_watcher_.reset();
	}
	config_watcher_dir_.clear();
}

bool ConfigManager::IsAutoReloadEnabled() const {
	return config_watcher_ && config_watcher_->IsRunning();
}

bool ConfigManager::LoadMongoDbConfigsFromServer() {
	// Called after server_config_ is populated (from string, file, or
	// programmatic assignment).  Suppress error output — the files may
	// legitimately not exist yet (e.g. first run before cluster setup).
	std::string dev_path;
	std::string public_path;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		dev_path = server_config_.mongodb_dev;
		public_path = server_config_.mongodb_public;
	}

	MongoDbConfig new_dev;
	MongoDbConfig new_public;
	bool dev_loaded = false;
	bool public_loaded = false;

	if (!dev_path.empty()) {
		dev_loaded = LoadMongoDbConfigFromFile(dev_path, new_dev);
		if (!dev_loaded) return false;
		CheckPlaintextCredentials(new_dev.connection.uri, "mongodb_dev");
	}
	if (!public_path.empty()) {
		public_loaded = LoadMongoDbConfigFromFile(public_path, new_public);
		if (!public_loaded) return false;
		CheckPlaintextCredentials(new_public.connection.uri, "mongodb_public");
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		mongo_dev_config_ = std::move(new_dev);
		mongo_public_config_ = std::move(new_public);
		mongo_dev_loaded_ = dev_loaded;
		mongo_public_loaded_ = public_loaded;
	}
	return true;
}

// ── Env-var interpolation ────────────────────────────────────────────────

std::string ConfigManager::InterpolateEnvVars(const std::string& value) {
	std::string result = value;
	std::regex re(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)(?::-([^}]*))?\})");
	std::smatch match;
	while (std::regex_search(result, match, re)) {
		std::string var_name = match[1].str();
		std::string default_val = match[2].matched ? match[2].str() : "";
		const char* env_val = std::getenv(var_name.c_str());
		std::string replacement = (env_val && env_val[0] != '\0') ? env_val : default_val;
		if (!match[2].matched && (!env_val || env_val[0] == '\0')) {
			if (auto* l = GetLogger())
				ENGINE_LOG_ERROR(l, "ConfigManager: env var ${} not set and no default", var_name);
		}
		result.replace(match.position(), match.length(), replacement);
	}
	return result;
}

// ── Validate ─────────────────────────────────────────────────────────────

ValidationResult ConfigManager::Validate() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	auto result = ConfigValidator::Validate(runtime_config_);
	auto srv_result = ConfigValidator::ValidateServer(server_config_);
	if (!srv_result.valid) {
		result.valid = false;
		if (!result.errors.empty()) result.errors += "; ";
		result.errors += srv_result.errors;
	}
	if (!srv_result.warnings.empty()) {
		if (!result.warnings.empty()) result.warnings += "; ";
		result.warnings += srv_result.warnings;
	}
	auto cross = ConfigValidator::ValidateCross(runtime_config_, server_config_);
	if (!cross.valid) {
		result.valid = false;
		if (!result.errors.empty()) result.errors += "; ";
		result.errors += cross.errors;
	}
	return result;
}

// ── Dump / snapshot API ──────────────────────────────────────────────────

std::string ConfigManager::Dump() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	auto rt_json = glz::write_json(runtime_config_);
	auto cc_json = glz::write_json(client_config_);
	auto srv_json = glz::write_json(server_config_);

	std::ostringstream oss;
	oss << "{";
	oss << "\"runtime\":" << (rt_json ? *rt_json : "\"\"");
	oss << ",\"client\":" << (cc_json ? *cc_json : "\"\"");
	oss << ",\"server\":" << (srv_json ? *srv_json : "\"\"");
	oss << "}";
	return oss.str();
}

std::string ConfigManager::DumpRuntime() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	auto result = glz::write_json(runtime_config_);
	return result ? *result : "{}";
}

std::string ConfigManager::DumpServer() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	auto result = glz::write_json(server_config_);
	return result ? *result : "{}";
}

// ── Dry-run validation ───────────────────────────────────────────────────

ConfigValidator::Result ConfigManager::ValidateOnly(const std::string& config_dir) {
	using namespace config;
	ConfigValidator::Result combined;

	// Parse runtime (required)
	{
		RuntimeConfig temp;
		std::string buf;
		auto ec = glz::read_file_json(temp, config_dir + kRuntimeConfigFile, buf);
		if (ec) {
			combined.valid = false;
			combined.errors += "runtime.json: parse error: " + glz::format_error(ec, buf);
		} else {
			ConfigValidator::Result vr = ConfigValidator::Validate(temp);
			if (!vr.valid) combined = vr;  // merge
		}
	}

	// Parse client (optional)
	{
		std::error_code ec;
		std::string client_path = config_dir + kClientConfigFile;
		if (std::filesystem::exists(client_path, ec)) {
			ClientConfig temp;
			std::string buf;
			auto ec2 = glz::read_file_json(temp, client_path, buf);
			if (ec2) {
				combined.valid = false;
				if (!combined.errors.empty()) combined.errors += "; ";
				combined.errors += "client.json: parse error: " + glz::format_error(ec2, buf);
			} else {
				ConfigValidator::Result vr = ConfigValidator::ValidateClient(temp);
				if (!vr.valid) {
					combined.valid = false;
					if (!combined.errors.empty()) combined.errors += "; ";
					combined.errors += vr.errors;
				}
			}
		}
	}

	// Parse server (optional)
	{
		std::error_code ec;
		std::string server_path = config_dir + kServerConfigFile;
		if (std::filesystem::exists(server_path, ec)) {
			ServerConfig temp;
			std::string buf;
			auto ec2 = glz::read_file_json(temp, server_path, buf);
			if (ec2) {
				combined.valid = false;
				if (!combined.errors.empty()) combined.errors += "; ";
				combined.errors += "server.json: parse error: " + glz::format_error(ec2, buf);
			} else {
				InterpolateConfigStrings(temp);
				ConfigValidator::Result vr = ConfigValidator::ValidateServer(temp);
				if (!vr.valid) {
					combined.valid = false;
					if (!combined.errors.empty()) combined.errors += "; ";
					combined.errors += vr.errors;
				}
				RedisClientConfig redis_config;
				bool redis_loaded = false;
				if (!LoadRedisConfigForServer(temp, redis_config, redis_loaded)) {
					combined.valid = false;
					if (!combined.errors.empty()) combined.errors += "; ";
					combined.errors += "redis config validation failed";
				}
			}
		}
	}

	return combined;
}

}  // namespace engine
