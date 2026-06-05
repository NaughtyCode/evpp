#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_config.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

#include <glaze/glaze.hpp>

namespace engine {

// Internal helpers

namespace {

constexpr uint64_t kMaxPhysicsConfigFileBytes = 1024ULL * 1024ULL;

// Read entire file to string, stripping BOM if present
std::string ReadFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) return {};
	std::streampos end = f.tellg();
	if (end < 0 || static_cast<uint64_t>(end) > kMaxPhysicsConfigFileBytes) return {};
	std::string s(static_cast<size_t>(end), '\0');
	f.seekg(0, std::ios::beg);
	if (!s.empty()) {
		f.read(s.data(), static_cast<std::streamsize>(s.size()));
		if (!f) return {};
	}
	// Strip UTF-8 BOM if present
	if (s.size() >= 3 && static_cast<uint8_t>(s[0]) == 0xEF && static_cast<uint8_t>(s[1]) == 0xBB &&
		static_cast<uint8_t>(s[2]) == 0xBF) {
		s.erase(0, 3);
	}
	return s;
}

bool FileExists(const std::string& path) {
	std::ifstream f(path);
	return f.good();
}

bool IsValidLogLevelName(const std::string& level) {
	return level == "trace" || level == "debug" || level == "info" || level == "warn" ||
		   level == "warning" || level == "error" || level == "fatal" || level == "critical";
}

}  // namespace

// Load all 4 JSON files

bool PhysicsConfigManager::Load(const std::string& config_dir) {
	std::lock_guard<std::shared_mutex> lock(config_mutex_);

	if (!LoadPhysics(config_dir + "/physics.json")) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: failed to load physics.json");
		return false;
	}
	if (!LoadThreading(config_dir + "/threading.json")) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: failed to load threading.json");
		return false;
	}
	if (!LoadLogging(config_dir + "/logging.json")) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: failed to load logging.json");
		return false;
	}
	if (!LoadThresholds(config_dir + "/thresholds.json")) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: failed to load thresholds.json");
		return false;
	}

	std::string error;
	if (!ValidateConfigs(error)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: validation failed: {}", error);
		return false;
	}

	return true;
}

// Per-file loaders

bool PhysicsConfigManager::LoadPhysics(const std::string& path) {
	if (!FileExists(path)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: file not found [{}]", path);
		return false;
	}
	std::string buf = ReadFile(path);
	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(physics_config_, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: parse error in [{}]: {}", path,
						 glz::format_error(ec, buf));
		return false;
	}
	return true;
}

bool PhysicsConfigManager::LoadThreading(const std::string& path) {
	if (!FileExists(path)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: file not found [{}]", path);
		return false;
	}
	std::string buf = ReadFile(path);
	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(threading_config_, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: parse error in [{}]: {}", path,
						 glz::format_error(ec, buf));
		return false;
	}
	return true;
}

bool PhysicsConfigManager::LoadLogging(const std::string& path) {
	if (!FileExists(path)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: file not found [{}]", path);
		return false;
	}
	std::string buf = ReadFile(path);
	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(log_config_, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: parse error in [{}]: {}", path,
						 glz::format_error(ec, buf));
		return false;
	}
	return true;
}

bool PhysicsConfigManager::LoadThresholds(const std::string& path) {
	if (!FileExists(path)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: file not found [{}]", path);
		return false;
	}
	std::string buf = ReadFile(path);
	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(thresholds_config_, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: parse error in [{}]: {}", path,
						 glz::format_error(ec, buf));
		return false;
	}
	return true;
}

// Validation

bool PhysicsConfigManager::ValidateConfigs(std::string& error_out) const {
	// PhysicsConfig validations
	auto finite = [](float value) { return std::isfinite(value); };
	if (!finite(physics_config_.gravity_x) || !finite(physics_config_.gravity_y) ||
		!finite(physics_config_.gravity_z)) {
		error_out = "gravity components must be finite";
		return false;
	}
	if (!finite(physics_config_.fixed_delta_time) || physics_config_.fixed_delta_time <= 0.0f) {
		error_out = "fixedDeltaTime must be > 0";
		return false;
	}
	if (physics_config_.scene_path.empty()) {
		error_out = "scenePath must not be empty";
		return false;
	}
	if (physics_config_.sub_step_count < 1 || physics_config_.sub_step_count > 16) {
		error_out = "subStepCount must be in [1, 16]";
		return false;
	}
	if (physics_config_.solver_iterations < 1 || physics_config_.solver_iterations > 128) {
		error_out = "solverIterations must be in [1, 128]";
		return false;
	}
	if (physics_config_.position_iterations < 0 || physics_config_.position_iterations > 16) {
		error_out = "positionIterations must be in [0, 16]";
		return false;
	}
	if (physics_config_.max_bodies < 1 || physics_config_.max_bodies > 8388608) {
		error_out = "maxBodies must be in [1, 8388608]";
		return false;
	}
	if (physics_config_.max_body_pairs < 1) {
		error_out = "maxBodyPairs must be > 0";
		return false;
	}
	if (physics_config_.max_body_pairs > 1'000'000) {
		error_out = "maxBodyPairs must be <= 1000000";
		return false;
	}
	if (physics_config_.max_contact_points < 1) {
		error_out = "maxContactPoints must be > 0";
		return false;
	}
	if (physics_config_.max_contact_points > 1'000'000) {
		error_out = "maxContactPoints must be <= 1000000";
		return false;
	}
	if (physics_config_.num_body_mutexes != 0) {
		// Must be power of 2 in [1, 64]
		int n = physics_config_.num_body_mutexes;
		if (n < 1 || n > 64 || (n & (n - 1)) != 0) {
			error_out = "numBodyMutexes must be 0 (auto) or power of 2 in [1, 64]";
			return false;
		}
	}
	if (!finite(physics_config_.speculative_contact_distance) ||
		physics_config_.speculative_contact_distance < 0.0f) {
		error_out = "speculativeContactDistance must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.penetration_slop) || physics_config_.penetration_slop < 0.0f) {
		error_out = "penetrationSlop must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.baumgarte) || physics_config_.baumgarte < 0.0f) {
		error_out = "baumgarte must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.time_before_sleep) || physics_config_.time_before_sleep < 0.0f) {
		error_out = "timeBeforeSleep must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.point_velocity_sleep_threshold) ||
		physics_config_.point_velocity_sleep_threshold < 0.0f) {
		error_out = "pointVelocitySleepThreshold must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.linear_cast_threshold) ||
		physics_config_.linear_cast_threshold < 0.0f) {
		error_out = "linearCastThreshold must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.linear_cast_max_penetration) ||
		physics_config_.linear_cast_max_penetration < 0.0f) {
		error_out = "linearCastMaxPenetration must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.max_penetration_distance) ||
		physics_config_.max_penetration_distance < 0.0f) {
		error_out = "maxPenetrationDistance must be finite and >= 0";
		return false;
	}
	if (!finite(physics_config_.min_velocity_for_restitution) ||
		physics_config_.min_velocity_for_restitution < 0.0f) {
		error_out = "minVelocityForRestitution must be finite and >= 0";
		return false;
	}

	// ThreadingConfig validations
	if (threading_config_.thread_priority != "high" && threading_config_.thread_priority != "low" &&
		threading_config_.thread_priority != "normal") {
		error_out = "threadPriority must be one of: high, normal, low";
		return false;
	}
	if (threading_config_.command_queue_size < 1) {
		error_out = "commandQueueSize must be > 0";
		return false;
	}
	if (threading_config_.command_queue_size > 1'000'000) {
		error_out = "commandQueueSize must be <= 1000000";
		return false;
	}
	if (threading_config_.result_queue_size < 1) {
		error_out = "resultQueueSize must be > 0";
		return false;
	}
	if (threading_config_.result_queue_size > 1'000'000) {
		error_out = "resultQueueSize must be <= 1000000";
		return false;
	}
	if (threading_config_.job_system_thread_count < -1) {
		error_out = "jobSystemThreadCount must be -1 (auto) or >= 0";
		return false;
	}
	if (threading_config_.job_system_max_jobs < 1) {
		error_out = "jobSystemMaxJobs must be > 0";
		return false;
	}
	if (threading_config_.job_system_max_barriers < 1 ||
		threading_config_.job_system_max_barriers > 8) {
		error_out = "jobSystemMaxBarriers must be in [1, 8]";
		return false;
	}
	if (threading_config_.max_pending_frames < 1) {
		error_out = "maxPendingFrames must be > 0";
		return false;
	}

	// ThresholdsConfig validations
	if (!finite(thresholds_config_.position_epsilon) || thresholds_config_.position_epsilon < 0.0f) {
		error_out = "positionEpsilon must be finite and >= 0";
		return false;
	}
	if (!finite(thresholds_config_.rotation_epsilon) || thresholds_config_.rotation_epsilon < 0.0f) {
		error_out = "rotationEpsilon must be finite and >= 0";
		return false;
	}
	if (!finite(thresholds_config_.linear_velocity_epsilon) ||
		thresholds_config_.linear_velocity_epsilon < 0.0f) {
		error_out = "linearVelocityEpsilon must be finite and >= 0";
		return false;
	}
	if (!finite(thresholds_config_.angular_velocity_epsilon) ||
		thresholds_config_.angular_velocity_epsilon < 0.0f) {
		error_out = "angularVelocityEpsilon must be finite and >= 0";
		return false;
	}

	// PhysicsLogConfig validations
	if (!IsValidLogLevelName(log_config_.level)) {
		error_out = "log level must be one of: trace, debug, info, warn, warning, error, fatal, critical";
		return false;
	}
	if (log_config_.max_file_size_mb < 1) {
		error_out = "maxFileSizeMb must be > 0";
		return false;
	}
	if (log_config_.max_backup_files < 0) {
		error_out = "maxBackupFiles must be >= 0";
		return false;
	}

	if (physics_config_.layer_config.object_layers.empty()) {
		error_out = "layerConfig.objectLayers must not be empty";
		return false;
	}
	if (physics_config_.layer_config.broad_phase_layers.empty()) {
		error_out = "layerConfig.broadPhaseLayers must not be empty";
		return false;
	}
	{
		std::unordered_set<uint16_t> object_layer_values;
		for (const auto& [name, value] : physics_config_.layer_config.object_layers) {
			if (name.empty()) {
				error_out = "object layer names must not be empty";
				return false;
			}
			if (!object_layer_values.insert(value).second) {
				error_out = "object layer values must be unique";
				return false;
			}
		}
	}
	{
		std::unordered_set<uint8_t> broad_phase_values;
		for (const auto& [name, value] : physics_config_.layer_config.broad_phase_layers) {
			if (name.empty()) {
				error_out = "broad phase layer names must not be empty";
				return false;
			}
			if (!broad_phase_values.insert(value).second) {
				error_out = "broad phase layer values must be unique";
				return false;
			}
		}
	}
	for (const auto& [name, value] : physics_config_.layer_config.broad_phase_layers) {
		(void) name;
		if (value > 63) {
			error_out = "broadPhaseLayers values must be <= 63";
			return false;
		}
	}
	for (const auto& [object_name, broad_phase_name] : physics_config_.layer_config.layer_mapping) {
		if (physics_config_.layer_config.object_layers.find(object_name) ==
			physics_config_.layer_config.object_layers.end()) {
			error_out = "layerMapping references unknown object layer: " + object_name;
			return false;
		}
		if (physics_config_.layer_config.broad_phase_layers.find(broad_phase_name) ==
			physics_config_.layer_config.broad_phase_layers.end()) {
			error_out = "layerMapping references unknown broad phase layer: " + broad_phase_name;
			return false;
		}
	}
	for (const auto& [object_name, _] : physics_config_.layer_config.object_layers) {
		if (physics_config_.layer_config.layer_mapping.find(object_name) ==
			physics_config_.layer_config.layer_mapping.end()) {
			error_out = "layerMapping missing object layer: " + object_name;
			return false;
		}
	}
	for (const auto& rule : physics_config_.layer_config.collision_matrix) {
		if (physics_config_.layer_config.object_layers.find(rule.layer_a) ==
				physics_config_.layer_config.object_layers.end() ||
			physics_config_.layer_config.object_layers.find(rule.layer_b) ==
				physics_config_.layer_config.object_layers.end()) {
			error_out = "collisionMatrix references unknown object layer";
			return false;
		}
	}

	return true;
}

// ── IConfigManager::Validate ──────────────────────────────────────────────

ValidationResult PhysicsConfigManager::Validate() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	ValidationResult result;
	std::string error;
	if (!ValidateConfigs(error)) {
		result.valid = false;
		result.errors = error;
	}
	return result;
}

// ── IConfigManager::Dump ──────────────────────────────────────────────────

std::string PhysicsConfigManager::Dump() const {
	std::shared_lock<std::shared_mutex> lock(config_mutex_);
	auto phys_json = glz::write_json(physics_config_);
	auto thread_json = glz::write_json(threading_config_);
	auto log_json = glz::write_json(log_config_);
	auto thresholds_json = glz::write_json(thresholds_config_);

	std::ostringstream oss;
	oss << "{";
	oss << "\"physics\":" << (phys_json ? *phys_json : "\"\"");
	oss << ",\"threading\":" << (thread_json ? *thread_json : "\"\"");
	oss << ",\"logging\":" << (log_json ? *log_json : "\"\"");
	oss << ",\"thresholds\":" << (thresholds_json ? *thresholds_json : "\"\"");
	oss << "}";
	return oss.str();
}

// ── IConfigManager::Reload (full) ─────────────────────────────────────────

bool PhysicsConfigManager::Reload(const std::string& config_dir) {
	PhysicsConfig new_physics;
	ThreadingConfig new_threading;
	PhysicsLogConfig new_log;
	ThresholdsConfig new_thresholds;

	std::string buf;
	glz::context ctx{};

	// Load physics.json
	buf = ReadFile(config_dir + "/physics.json");
	if (buf.empty()) return false;
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_physics, buf, ctx);
	if (ec) return false;

	// Load threading.json
	buf = ReadFile(config_dir + "/threading.json");
	if (buf.empty()) return false;
	ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_threading, buf, ctx);
	if (ec) return false;

	// Load logging.json
	buf = ReadFile(config_dir + "/logging.json");
	if (buf.empty()) return false;
	ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_log, buf, ctx);
	if (ec) return false;

	// Load thresholds.json
	buf = ReadFile(config_dir + "/thresholds.json");
	if (buf.empty()) return false;
	ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_thresholds, buf, ctx);
	if (ec) return false;

	// Validate new configs before swapping under lock
	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);

		PhysicsConfig old_physics = std::move(physics_config_);
		ThreadingConfig old_threading = std::move(threading_config_);
		PhysicsLogConfig old_log = std::move(log_config_);
		ThresholdsConfig old_thresholds = std::move(thresholds_config_);

		physics_config_ = std::move(new_physics);
		threading_config_ = std::move(new_threading);
		log_config_ = std::move(new_log);
		thresholds_config_ = std::move(new_thresholds);

		std::string error;
		if (!ValidateConfigs(error)) {
			// Rollback on validation failure
			physics_config_ = std::move(old_physics);
			threading_config_ = std::move(old_threading);
			log_config_ = std::move(old_log);
			thresholds_config_ = std::move(old_thresholds);
			ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: reload validation failed: {}", error);
			return false;
		}
	}

	return true;
}

// Hot-reload

bool PhysicsConfigManager::ReloadThresholds(const std::string& config_dir) {
	ThresholdsConfig new_cfg;
	std::string path = config_dir + "/thresholds.json";
	std::string buf = ReadFile(path);
	if (buf.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: thresholds.json not found");
		return false;
	}

	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_cfg, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: thresholds reload parse error: {}",
						 glz::format_error(ec, buf));
		return false;
	}

	// Validate new thresholds
	if (!std::isfinite(new_cfg.position_epsilon) || new_cfg.position_epsilon < 0.0f ||
		!std::isfinite(new_cfg.rotation_epsilon) || new_cfg.rotation_epsilon < 0.0f ||
		!std::isfinite(new_cfg.linear_velocity_epsilon) ||
		new_cfg.linear_velocity_epsilon < 0.0f ||
		!std::isfinite(new_cfg.angular_velocity_epsilon) ||
		new_cfg.angular_velocity_epsilon < 0.0f) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: invalid threshold values");
		return false;
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		thresholds_config_ = std::move(new_cfg);
	}
	return true;
}

bool PhysicsConfigManager::ReloadLogLevel(const std::string& config_dir) {
	std::string path = config_dir + "/logging.json";
	std::string buf = ReadFile(path);
	if (buf.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: logging.json not found");
		return false;
	}

	PhysicsLogConfig new_cfg;
	glz::context ctx{};
	auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(new_cfg, buf, ctx);
	if (ec) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: logging reload parse error: {}",
						 glz::format_error(ec, buf));
		return false;
	}
	if (!IsValidLogLevelName(new_cfg.level)) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "PhysicsConfigManager: invalid logging level '{}'",
						 new_cfg.level);
		return false;
	}

	{
		std::lock_guard<std::shared_mutex> lock(config_mutex_);
		// Only update the level field (the hot-reloadable field)
		log_config_.level = std::move(new_cfg.level);
	}
	return true;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
