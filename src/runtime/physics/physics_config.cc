#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_config.h"

#include <cstdio>
#include <fstream>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

#include <glaze/glaze.hpp>

namespace engine {

// Internal helpers

namespace {

// Read entire file to string, stripping BOM if present
std::string ReadFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::string s{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
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

}  // namespace

// Load all 4 JSON files

bool PhysicsConfigManager::Load(const std::string& config_dir) {
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
	if (physics_config_.fixed_delta_time <= 0.0f) {
		error_out = "fixedDeltaTime must be > 0";
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
	if (physics_config_.max_contact_points < 1) {
		error_out = "maxContactPoints must be > 0";
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

	// ThreadingConfig validations
	if (threading_config_.command_queue_size < 1) {
		error_out = "commandQueueSize must be > 0";
		return false;
	}
	if (threading_config_.result_queue_size < 1) {
		error_out = "resultQueueSize must be > 0";
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
	if (thresholds_config_.position_epsilon < 0.0f) {
		error_out = "positionEpsilon must be >= 0";
		return false;
	}
	if (thresholds_config_.rotation_epsilon < 0.0f) {
		error_out = "rotationEpsilon must be >= 0";
		return false;
	}
	if (thresholds_config_.linear_velocity_epsilon < 0.0f) {
		error_out = "linearVelocityEpsilon must be >= 0";
		return false;
	}
	if (thresholds_config_.angular_velocity_epsilon < 0.0f) {
		error_out = "angularVelocityEpsilon must be >= 0";
		return false;
	}

	// PhysicsLogConfig validations
	if (log_config_.max_file_size_mb < 1) {
		error_out = "maxFileSizeMb must be > 0";
		return false;
	}
	if (log_config_.max_backup_files < 0) {
		error_out = "maxBackupFiles must be >= 0";
		return false;
	}

	return true;
}

// Hot-reload

bool PhysicsConfigManager::ReloadThresholds(const std::string& config_dir) {
	// Single-threaded: only called from PhysicsSystem::ReloadThresholds()
	// on the main thread. No concurrent access — no lock needed.
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
	if (new_cfg.position_epsilon < 0.0f || new_cfg.rotation_epsilon < 0.0f ||
		new_cfg.linear_velocity_epsilon < 0.0f || new_cfg.angular_velocity_epsilon < 0.0f) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "PhysicsConfigManager: invalid threshold values");
		return false;
	}

	thresholds_config_ = new_cfg;
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

	// Only update the level field (the hot-reloadable field)
	log_config_.level = std::move(new_cfg.level);
	return true;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
