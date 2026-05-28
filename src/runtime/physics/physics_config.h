#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glaze/glaze.hpp>

namespace engine {

// LayerConfig — collision layer definitions (nested in physics.json)

struct LayerConfig {
	std::unordered_map<std::string, uint16_t> object_layers;
	std::unordered_map<std::string, uint8_t> broad_phase_layers;
	std::unordered_map<std::string, std::string> layer_mapping;

	struct CollisionRule {
		std::string layer_a;
		std::string layer_b;
		bool collide = true;
	};
	std::vector<CollisionRule> collision_matrix;
};

// PhysicsConfig — core physics parameters (physics.json)

struct PhysicsConfig {
	// Design doc §5.1 required fields
	float gravity_x = 0.0f;
	float gravity_y = -9.81f;
	float gravity_z = 0.0f;
	float fixed_delta_time = 0.01667f;
	int solver_iterations = 10;
	int sub_step_count = 2;
	int max_bodies = 4096;
	int max_contact_points = 10240;
	int max_body_pairs = 16384;
	int num_body_mutexes = 32;

	// JoltPhysics supplementary fields (aligned with PhysicsSettings)
	int position_iterations = 2;
	float speculative_contact_distance = 0.02f;
	float penetration_slop = 0.02f;
	float baumgarte = 0.2f;
	float time_before_sleep = 0.5f;
	float point_velocity_sleep_threshold = 0.03f;
	bool deterministic_simulation = true;
	bool constraint_warm_start = true;
	bool allow_sleeping = true;
	bool use_large_island_splitter = true;
	float linear_cast_threshold = 0.75f;
	float linear_cast_max_penetration = 0.25f;
	float max_penetration_distance = 0.2f;
	float min_velocity_for_restitution = 1.0f;
	bool use_body_pair_contact_cache = true;
	bool use_manifold_reduction = true;
	bool check_active_edges = true;

	// Nested collision layer config
	LayerConfig layer_config;
};

// ThreadingConfig — thread model configuration (threading.json)

struct ThreadingConfig {
	// Design doc §5.2 fields
	std::string thread_priority = "high";
	uint64_t affinity_mask = 0;
	int command_queue_size = 256;
	int result_queue_size = 64;

	// Supplementary fields
	int max_pending_frames = 3;
	int job_system_max_jobs = 2048;
	int job_system_max_barriers = 8;
	int job_system_thread_count = -1;
};

// PhysicsLogConfig — physics log configuration (logging.json)

struct PhysicsLogConfig {
	// Design doc §5.3 fields
	std::string log_dir = "./logs/physics";
	std::string file_name = "physics_engine";
	std::string date_format = "YYYY-MM-DD";
	std::string level = "info";
	int max_file_size_mb = 50;
	int max_backup_files = 10;

	// Supplementary fields (aligned with engine::LogConfig)
	std::string rotation_frequency = "";
	int rotation_interval = 1;
	std::string rotation_time_daily = "00:00";
	std::string format_pattern =
		"%(time) [%(thread_id)] [%(log_level_short_code)] [%(short_source_location)] "
		"[%(caller_function)] [%(logger)] %(message)";
};

// ThresholdsConfig — change detection thresholds (thresholds.json)

struct ThresholdsConfig {
	float position_epsilon = 0.001f;  // 1mm
	float rotation_epsilon = 0.00017f;	// ~0.01°
	float linear_velocity_epsilon = 0.01f;	// 0.01 m/s
	float angular_velocity_epsilon = 0.001f;  // 0.001 rad/s
};

}  // namespace engine

// glaze reflection metadata — JSON key names use camelCase per design doc

template <>
struct glz::meta<engine::LayerConfig::CollisionRule> {
	using T = engine::LayerConfig::CollisionRule;
	static constexpr auto value =
		glz::object("layerA", &T::layer_a, "layerB", &T::layer_b, "collide", &T::collide);
};

template <>
struct glz::meta<engine::LayerConfig> {
	using T = engine::LayerConfig;
	static constexpr auto value = glz::object("objectLayers",
											  &T::object_layers,
											  "broadPhaseLayers",
											  &T::broad_phase_layers,
											  "layerMapping",
											  &T::layer_mapping,
											  "collisionMatrix",
											  &T::collision_matrix);
};

template <>
struct glz::meta<engine::PhysicsConfig> {
	using T = engine::PhysicsConfig;
	static constexpr auto value = glz::object("gravityX",
											  &T::gravity_x,
											  "gravityY",
											  &T::gravity_y,
											  "gravityZ",
											  &T::gravity_z,
											  "fixedDeltaTime",
											  &T::fixed_delta_time,
											  "solverIterations",
											  &T::solver_iterations,
											  "subStepCount",
											  &T::sub_step_count,
											  "maxBodies",
											  &T::max_bodies,
											  "maxContactPoints",
											  &T::max_contact_points,
											  "maxBodyPairs",
											  &T::max_body_pairs,
											  "numBodyMutexes",
											  &T::num_body_mutexes,
											  "positionIterations",
											  &T::position_iterations,
											  "speculativeContactDistance",
											  &T::speculative_contact_distance,
											  "penetrationSlop",
											  &T::penetration_slop,
											  "baumgarte",
											  &T::baumgarte,
											  "timeBeforeSleep",
											  &T::time_before_sleep,
											  "pointVelocitySleepThreshold",
											  &T::point_velocity_sleep_threshold,
											  "deterministicSimulation",
											  &T::deterministic_simulation,
											  "constraintWarmStart",
											  &T::constraint_warm_start,
											  "allowSleeping",
											  &T::allow_sleeping,
											  "useLargeIslandSplitter",
											  &T::use_large_island_splitter,
											  "linearCastThreshold",
											  &T::linear_cast_threshold,
											  "linearCastMaxPenetration",
											  &T::linear_cast_max_penetration,
											  "maxPenetrationDistance",
											  &T::max_penetration_distance,
											  "minVelocityForRestitution",
											  &T::min_velocity_for_restitution,
											  "useBodyPairContactCache",
											  &T::use_body_pair_contact_cache,
											  "useManifoldReduction",
											  &T::use_manifold_reduction,
											  "checkActiveEdges",
											  &T::check_active_edges,
											  "layerConfig",
											  &T::layer_config);
};

template <>
struct glz::meta<engine::ThreadingConfig> {
	using T = engine::ThreadingConfig;
	static constexpr auto value = glz::object("threadPriority",
											  &T::thread_priority,
											  "affinityMask",
											  &T::affinity_mask,
											  "commandQueueSize",
											  &T::command_queue_size,
											  "resultQueueSize",
											  &T::result_queue_size,
											  "maxPendingFrames",
											  &T::max_pending_frames,
											  "jobSystemMaxJobs",
											  &T::job_system_max_jobs,
											  "jobSystemMaxBarriers",
											  &T::job_system_max_barriers,
											  "jobSystemThreadCount",
											  &T::job_system_thread_count);
};

template <>
struct glz::meta<engine::PhysicsLogConfig> {
	using T = engine::PhysicsLogConfig;
	static constexpr auto value = glz::object("logDir",
											  &T::log_dir,
											  "fileName",
											  &T::file_name,
											  "dateFormat",
											  &T::date_format,
											  "level",
											  &T::level,
											  "maxFileSizeMb",
											  &T::max_file_size_mb,
											  "maxBackupFiles",
											  &T::max_backup_files,
											  "rotationFrequency",
											  &T::rotation_frequency,
											  "rotationInterval",
											  &T::rotation_interval,
											  "rotationTimeDaily",
											  &T::rotation_time_daily,
											  "formatPattern",
											  &T::format_pattern);
};

template <>
struct glz::meta<engine::ThresholdsConfig> {
	using T = engine::ThresholdsConfig;
	static constexpr auto value = glz::object("positionEpsilon",
											  &T::position_epsilon,
											  "rotationEpsilon",
											  &T::rotation_epsilon,
											  "linearVelocityEpsilon",
											  &T::linear_velocity_epsilon,
											  "angularVelocityEpsilon",
											  &T::angular_velocity_epsilon);
};

namespace engine {

// PhysicsConfigManager — independent config manager (not dependent on
// engine::ConfigManager). Owns all 4 config structs and handles loading,
// validation, and hot-reload of supported fields.

class PhysicsConfigManager {
	public:
	PhysicsConfigManager() = default;

	// Load all 4 JSON files from config_dir.
	// Returns false if any file is missing or has format errors [D22].
	bool Load(const std::string& config_dir);

	// Runtime hot-reload (only fields marked for hot-reload)
	bool ReloadThresholds(const std::string& config_dir);
	bool ReloadLogLevel(const std::string& config_dir);

	// Accessors
	const PhysicsConfig& GetPhysicsConfig() const {
		return physics_config_;
	}
	const ThreadingConfig& GetThreadingConfig() const {
		return threading_config_;
	}
	const PhysicsLogConfig& GetLogConfig() const {
		return log_config_;
	}
	const ThresholdsConfig& GetThresholdsConfig() const {
		return thresholds_config_;
	}

	// Mutable access for hot-reload atomic swap
	ThresholdsConfig& GetThresholdsConfigMutable() {
		return thresholds_config_;
	}

	private:
	bool LoadPhysics(const std::string& path);
	bool LoadThreading(const std::string& path);
	bool LoadLogging(const std::string& path);
	bool LoadThresholds(const std::string& path);

	// Validate config values are within legal ranges
	bool ValidateConfigs(std::string& error_out) const;

	PhysicsConfig physics_config_;
	ThreadingConfig threading_config_;
	PhysicsLogConfig log_config_;
	ThresholdsConfig thresholds_config_;
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
