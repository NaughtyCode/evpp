#include "runtime/config/config.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <sstream>

#include <glaze/glaze.hpp>

#include "runtime/config/config_constants.h"
#include "runtime/config/config_validator.h"
#include "runtime/core/log/log.h"
#include "runtime/database/data_service/db_service_config.h"

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

}  // namespace

ConfigManager& ConfigManager::Instance() {
	static ConfigManager instance;
	return instance;
}

// From JSON strings (text)

bool ConfigManager::LoadRuntimeFromString(const std::string& json) {
	RuntimeConfig temp;
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse runtime config: {}", glz::format_error(ec, json));
		return false;
	}
	auto vr = ConfigValidator::Validate(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: runtime config validation failed: {}", vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		runtime_config_ = std::move(temp);
	}
	return true;
}

bool ConfigManager::LoadClientFromString(const std::string& json) {
	ClientConfig temp;
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse client config: {}", glz::format_error(ec, json));
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		client_config_ = std::move(temp);
	}
	return true;
}

bool ConfigManager::LoadServerFromString(const std::string& json) {
	ServerConfig temp;
	auto ec = glz::read_json(temp, json);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to parse server config: {}", glz::format_error(ec, json));
		return false;
	}
	auto vr = ConfigValidator::ValidateServer(temp);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: server config validation failed: {}", vr.errors);
		return false;
	}
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		server_config_ = std::move(temp);
	}
	LoadMongoDbConfigsFromServer();
	return true;
}

// From files

bool ConfigManager::LoadRuntimeFromFile(const std::string& path) {
	std::string buf;
	auto ec = glz::read_file_json(runtime_config_, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	auto vr = ConfigValidator::Validate(runtime_config_);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: runtime config validation failed [{}]: {}", path, vr.errors);
		return false;
	}
	return true;
}

bool ConfigManager::LoadClientFromFile(const std::string& path) {
	std::string buf;
	auto ec = glz::read_file_json(client_config_, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	return true;
}

bool ConfigManager::LoadServerFromFile(const std::string& path) {
	std::string buf;
	auto ec = glz::read_file_json(server_config_, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
	auto vr = ConfigValidator::ValidateServer(server_config_);
	if (!vr.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: server config validation failed [{}]: {}", path, vr.errors);
		return false;
	}
	LoadMongoDbConfigsFromServer();
	return true;
}

bool ConfigManager::Load(const std::string& config_dir) {
	using namespace config;

	// Runtime config is required — both client and server need it.
	std::string runtime_path = config_dir + kRuntimeConfigFile;
	if (!LoadRuntimeFromFile(runtime_path)) return false;

	// Client and server configs are optional — one may not exist
	// depending on the build target.  Check existence first to avoid
	// spurious "failed to load" messages on stderr.
	std::error_code ec;
	bool have_client = false;
	bool have_server = false;
	std::string client_path = config_dir + kClientConfigFile;
	if (std::filesystem::exists(client_path, ec)) {
		have_client = LoadClientFromFile(client_path);
	}
	std::string server_path = config_dir + kServerConfigFile;
	if (std::filesystem::exists(server_path, ec)) {
		have_server = LoadServerFromFile(server_path);
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
	{
		auto vr = ConfigValidator::Validate(new_runtime);
		if (!vr.valid) {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload validation failed for runtime.json: {}",
							 vr.errors);
			return false;
		}
	}

	bool have_client = false;
	std::error_code ec2;
	std::string client_path = config_dir + kClientConfigFile;
	if (std::filesystem::exists(client_path, ec2)) {
		buf.clear();
		auto ec3 = glz::read_file_json(new_client, client_path, buf);
		if (!ec3) {
			have_client = true;
		}
	}

	bool have_server = false;
	std::string server_path = config_dir + kServerConfigFile;
	if (std::filesystem::exists(server_path, ec2)) {
		buf.clear();
		auto ec3 = glz::read_file_json(new_server, server_path, buf);
		if (!ec3) {
			have_server = true;
		} else {
			ENGINE_LOG_ERROR(logger, "ConfigManager: reload parse error for server.json: {}",
							 glz::format_error(ec3, buf));
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
	if (have_server) {
		if (!new_server.mongodb_dev.empty()) {
			dev_loaded = LoadMongoDbConfigFromFile(new_server.mongodb_dev, new_mongo_dev);
		}
		if (!new_server.mongodb_public.empty()) {
			public_loaded = LoadMongoDbConfigFromFile(new_server.mongodb_public, new_mongo_public);
		}
	}

	// Snapshot old values before swap for diff logging.
	RuntimeConfig old_runtime;
	ServerConfig old_server;
	{
		std::shared_lock<std::shared_mutex> lock(config_mutex_);
		old_runtime = runtime_config_;
		old_server = server_config_;
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
		}
	}

	// Field-level diff logging.
	{
		auto& rt = runtime_config_;
		int changes = 0;
		if (old_runtime.frame.target_fps != rt.frame.target_fps) {
			ENGINE_LOG_INFO(logger, "config: frame.target_fps: {} -> {}",
							old_runtime.frame.target_fps, rt.frame.target_fps);
			changes++;
		}
		if (old_runtime.frame.interval_ms != rt.frame.interval_ms) {
			ENGINE_LOG_INFO(logger, "config: frame.interval_ms: {} -> {}",
							old_runtime.frame.interval_ms, rt.frame.interval_ms);
			changes++;
		}
		if (old_runtime.log.level != rt.log.level) {
			ENGINE_LOG_INFO(logger, "config: log.level: {} -> {}",
							old_runtime.log.level, rt.log.level);
			changes++;
		}
		if (old_runtime.sandbox_level != rt.sandbox_level) {
			ENGINE_LOG_INFO(logger, "config: sandbox_level: {} -> {}",
							old_runtime.sandbox_level, rt.sandbox_level);
			changes++;
		}
		if (old_runtime.resource_dir != rt.resource_dir) {
			ENGINE_LOG_INFO(logger, "config: resource_dir: {} -> {}",
							old_runtime.resource_dir, rt.resource_dir);
			changes++;
		}
		if (old_server.admin_port != server_config_.admin_port) {
			ENGINE_LOG_INFO(logger, "config: admin_port: {} -> {}",
							old_server.admin_port, server_config_.admin_port);
			changes++;
		}
		ENGINE_LOG_INFO(logger, "ConfigManager: config reloaded ({} fields changed)", changes);
	}

	NotifyReloadCallbacks();
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
	return true;
}

bool ConfigManager::LoadDbServiceConfigFromFile(const std::string& path, DbServiceConfig& out) {
	std::string buf;
	auto ec = glz::read_file_json(out, path, buf);
	if (ec) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "ConfigManager: failed to load db_service config [{}]: {}", path, glz::format_error(ec, buf));
		return false;
	}
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

void ConfigManager::NotifyReloadCallbacks() {
	if (reloading_.exchange(true)) return;  // prevent re-entrant reload

	std::vector<std::pair<int, ReloadCallback>> callbacks_copy;
	{
		std::lock_guard<std::mutex> lock(callbacks_mutex_);
		callbacks_copy = callbacks_;
	}
	auto* logger = GetLogger();
	for (auto& [id, callback] : callbacks_copy) {
		try {
			callback();
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

void ConfigManager::LoadMongoDbConfigsFromServer() {
	// Called after server_config_ is populated (from string, file, or
	// programmatic assignment).  Suppress error output — the files may
	// legitimately not exist yet (e.g. first run before cluster setup).
	if (!server_config_.mongodb_dev.empty()) {
		mongo_dev_loaded_ =
			LoadMongoDbConfigFromFile(server_config_.mongodb_dev, mongo_dev_config_);
		if (mongo_dev_loaded_) CheckPlaintextCredentials(mongo_dev_config_.connection.uri, "mongodb_dev");
	}
	if (!server_config_.mongodb_public.empty()) {
		mongo_public_loaded_ =
			LoadMongoDbConfigFromFile(server_config_.mongodb_public, mongo_public_config_);
		if (mongo_public_loaded_) CheckPlaintextCredentials(mongo_public_config_.connection.uri, "mongodb_public");
	}
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

// ── Dump / snapshot API ──────────────────────────────────────────────────

std::string ConfigManager::Dump() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	std::ostringstream oss;
	oss << "{";
	oss << "\"runtime\":" << glz::write_json(runtime_config_);
	oss << ",\"client\":" << glz::write_json(client_config_);
	oss << ",\"server\":" << glz::write_json(server_config_);
	oss << "}";
	return oss.str();
}

std::string ConfigManager::DumpRuntime() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return glz::write_json(runtime_config_);
}

std::string ConfigManager::DumpServer() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	return glz::write_json(server_config_);
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

	// Parse server (optional)
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
			ConfigValidator::Result vr = ConfigValidator::ValidateServer(temp);
			if (!vr.valid) {
				combined.valid = false;
				if (!combined.errors.empty()) combined.errors += "; ";
				combined.errors += vr.errors;
			}
		}
	}

	return combined;
}

}  // namespace engine
