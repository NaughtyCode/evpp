#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace engine {
namespace physics_bindings {

namespace {

template <typename Value, typename PushValue>
void PushStringMap(lua_State* L,
				   const std::unordered_map<std::string, Value>& values,
				   PushValue push_value) {
	lua_newtable(L);
	for (const auto& [key, value] : values) {
		push_value(L, value);
		lua_setfield(L, -2, key.c_str());
	}
}

void PushObjectLayerMap(lua_State* L, const std::unordered_map<std::string, uint16_t>& values) {
	PushStringMap(L, values, [](lua_State* state, uint16_t value) {
		lua_pushinteger(state, static_cast<lua_Integer>(value));
	});
}

void PushBroadPhaseLayerMap(lua_State* L, const std::unordered_map<std::string, uint8_t>& values) {
	PushStringMap(L, values, [](lua_State* state, uint8_t value) {
		lua_pushinteger(state, static_cast<lua_Integer>(value));
	});
}

void PushStringStringMap(lua_State* L, const std::unordered_map<std::string, std::string>& values) {
	PushStringMap(L, values, [](lua_State* state, const std::string& value) {
		lua_pushlstring(state, value.data(), value.size());
	});
}

void PushLayerConfig(lua_State* L, const LayerConfig& config) {
	lua_newtable(L);

	PushObjectLayerMap(L, config.object_layers);
	lua_setfield(L, -2, "object_layers");
	PushObjectLayerMap(L, config.object_layers);
	lua_setfield(L, -2, "objectLayers");

	PushBroadPhaseLayerMap(L, config.broad_phase_layers);
	lua_setfield(L, -2, "broad_phase_layers");
	PushBroadPhaseLayerMap(L, config.broad_phase_layers);
	lua_setfield(L, -2, "broadPhaseLayers");

	PushStringStringMap(L, config.layer_mapping);
	lua_setfield(L, -2, "layer_mapping");
	PushStringStringMap(L, config.layer_mapping);
	lua_setfield(L, -2, "layerMapping");

	lua_newtable(L);
	for (size_t i = 0; i < config.collision_matrix.size(); ++i) {
		const auto& rule = config.collision_matrix[i];
		lua_newtable(L);
		SetField(L, "layer_a", rule.layer_a);
		SetField(L, "layerA", rule.layer_a);
		SetField(L, "layer_b", rule.layer_b);
		SetField(L, "layerB", rule.layer_b);
		SetField(L, "collide", rule.collide);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "collision_matrix");
	lua_getfield(L, -1, "collision_matrix");
	lua_setfield(L, -2, "collisionMatrix");
}

void PushPhysicsConfig(lua_State* L, const PhysicsConfig& config) {
	lua_newtable(L);
	SetField(L, "version", config.version);
	SetField(L, "scene_path", config.scene_path);
	SetField(L, "scenePath", config.scene_path);
	SetField(L, "gravity_x", config.gravity_x);
	SetField(L, "gravityX", config.gravity_x);
	SetField(L, "gravity_y", config.gravity_y);
	SetField(L, "gravityY", config.gravity_y);
	SetField(L, "gravity_z", config.gravity_z);
	SetField(L, "gravityZ", config.gravity_z);
	SetField(L, "fixed_delta_time", config.fixed_delta_time);
	SetField(L, "fixedDeltaTime", config.fixed_delta_time);
	SetField(L, "solver_iterations", config.solver_iterations);
	SetField(L, "solverIterations", config.solver_iterations);
	SetField(L, "sub_step_count", config.sub_step_count);
	SetField(L, "subStepCount", config.sub_step_count);
	SetField(L, "max_bodies", config.max_bodies);
	SetField(L, "maxBodies", config.max_bodies);
	SetField(L, "max_contact_points", config.max_contact_points);
	SetField(L, "maxContactPoints", config.max_contact_points);
	SetField(L, "max_body_pairs", config.max_body_pairs);
	SetField(L, "maxBodyPairs", config.max_body_pairs);
	SetField(L, "num_body_mutexes", config.num_body_mutexes);
	SetField(L, "numBodyMutexes", config.num_body_mutexes);
	SetField(L, "position_iterations", config.position_iterations);
	SetField(L, "positionIterations", config.position_iterations);
	SetField(L, "speculative_contact_distance", config.speculative_contact_distance);
	SetField(L, "speculativeContactDistance", config.speculative_contact_distance);
	SetField(L, "penetration_slop", config.penetration_slop);
	SetField(L, "penetrationSlop", config.penetration_slop);
	SetField(L, "baumgarte", config.baumgarte);
	SetField(L, "time_before_sleep", config.time_before_sleep);
	SetField(L, "timeBeforeSleep", config.time_before_sleep);
	SetField(L, "point_velocity_sleep_threshold", config.point_velocity_sleep_threshold);
	SetField(L, "pointVelocitySleepThreshold", config.point_velocity_sleep_threshold);
	SetField(L, "deterministic_simulation", config.deterministic_simulation);
	SetField(L, "deterministicSimulation", config.deterministic_simulation);
	SetField(L, "constraint_warm_start", config.constraint_warm_start);
	SetField(L, "constraintWarmStart", config.constraint_warm_start);
	SetField(L, "allow_sleeping", config.allow_sleeping);
	SetField(L, "allowSleeping", config.allow_sleeping);
	SetField(L, "use_large_island_splitter", config.use_large_island_splitter);
	SetField(L, "useLargeIslandSplitter", config.use_large_island_splitter);
	SetField(L, "linear_cast_threshold", config.linear_cast_threshold);
	SetField(L, "linearCastThreshold", config.linear_cast_threshold);
	SetField(L, "linear_cast_max_penetration", config.linear_cast_max_penetration);
	SetField(L, "linearCastMaxPenetration", config.linear_cast_max_penetration);
	SetField(L, "max_penetration_distance", config.max_penetration_distance);
	SetField(L, "maxPenetrationDistance", config.max_penetration_distance);
	SetField(L, "min_velocity_for_restitution", config.min_velocity_for_restitution);
	SetField(L, "minVelocityForRestitution", config.min_velocity_for_restitution);
	SetField(L, "use_body_pair_contact_cache", config.use_body_pair_contact_cache);
	SetField(L, "useBodyPairContactCache", config.use_body_pair_contact_cache);
	SetField(L, "use_manifold_reduction", config.use_manifold_reduction);
	SetField(L, "useManifoldReduction", config.use_manifold_reduction);
	SetField(L, "check_active_edges", config.check_active_edges);
	SetField(L, "checkActiveEdges", config.check_active_edges);
	PushLayerConfig(L, config.layer_config);
	lua_setfield(L, -2, "layer_config");
	PushLayerConfig(L, config.layer_config);
	lua_setfield(L, -2, "layerConfig");
}

void PushThreadingConfig(lua_State* L, const ThreadingConfig& config) {
	lua_newtable(L);
	SetField(L, "version", config.version);
	SetField(L, "thread_priority", config.thread_priority);
	SetField(L, "threadPriority", config.thread_priority);
	SetField(L, "affinity_mask", config.affinity_mask);
	SetField(L, "affinityMask", config.affinity_mask);
	SetField(L, "command_queue_size", config.command_queue_size);
	SetField(L, "commandQueueSize", config.command_queue_size);
	SetField(L, "result_queue_size", config.result_queue_size);
	SetField(L, "resultQueueSize", config.result_queue_size);
	SetField(L, "max_pending_frames", config.max_pending_frames);
	SetField(L, "maxPendingFrames", config.max_pending_frames);
	SetField(L, "job_system_max_jobs", config.job_system_max_jobs);
	SetField(L, "jobSystemMaxJobs", config.job_system_max_jobs);
	SetField(L, "job_system_max_barriers", config.job_system_max_barriers);
	SetField(L, "jobSystemMaxBarriers", config.job_system_max_barriers);
	SetField(L, "job_system_thread_count", config.job_system_thread_count);
	SetField(L, "jobSystemThreadCount", config.job_system_thread_count);
}

void PushLogConfig(lua_State* L, const PhysicsLogConfig& config) {
	lua_newtable(L);
	SetField(L, "version", config.version);
	SetField(L, "log_dir", config.log_dir);
	SetField(L, "logDir", config.log_dir);
	SetField(L, "file_name", config.file_name);
	SetField(L, "fileName", config.file_name);
	SetField(L, "date_format", config.date_format);
	SetField(L, "dateFormat", config.date_format);
	SetField(L, "level", config.level);
	SetField(L, "max_file_size_mb", config.max_file_size_mb);
	SetField(L, "maxFileSizeMb", config.max_file_size_mb);
	SetField(L, "max_backup_files", config.max_backup_files);
	SetField(L, "maxBackupFiles", config.max_backup_files);
	SetField(L, "rotation_frequency", config.rotation_frequency);
	SetField(L, "rotationFrequency", config.rotation_frequency);
	SetField(L, "rotation_interval", config.rotation_interval);
	SetField(L, "rotationInterval", config.rotation_interval);
	SetField(L, "rotation_time_daily", config.rotation_time_daily);
	SetField(L, "rotationTimeDaily", config.rotation_time_daily);
	SetField(L, "format_pattern", config.format_pattern);
	SetField(L, "formatPattern", config.format_pattern);
}

void PushThresholdsConfig(lua_State* L, const ThresholdsConfig& config) {
	lua_newtable(L);
	SetField(L, "version", config.version);
	SetField(L, "position_epsilon", config.position_epsilon);
	SetField(L, "positionEpsilon", config.position_epsilon);
	SetField(L, "rotation_epsilon", config.rotation_epsilon);
	SetField(L, "rotationEpsilon", config.rotation_epsilon);
	SetField(L, "linear_velocity_epsilon", config.linear_velocity_epsilon);
	SetField(L, "linearVelocityEpsilon", config.linear_velocity_epsilon);
	SetField(L, "angular_velocity_epsilon", config.angular_velocity_epsilon);
	SetField(L, "angularVelocityEpsilon", config.angular_velocity_epsilon);
}

template <typename Config, typename Getter, typename Pusher>
int PushConfigSnapshot(lua_State* L, Getter getter, Pusher pusher, const char* error) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	std::optional<Config> config = (ctx.system->*getter)();
	if (!config.has_value()) {
		return PushNilError(L, error);
	}
	pusher(L, *config);
	return 1;
}

int LuaIsInitialized(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_pushboolean(L, ctx.system->IsInitialized() ? 1 : 0);
	return 1;
}

int LuaIsRunning(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_pushboolean(L, ctx.system->IsRunning() ? 1 : 0);
	return 1;
}

int LuaIsHealthy(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_pushboolean(L, ctx.system->IsHealthy() ? 1 : 0);
	return 1;
}

int LuaGetFixedDeltaTime(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_pushnumber(L, ctx.system->GetFixedDeltaTime());
	return 1;
}

int LuaStatus(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_newtable(L);
	SetField(L, "initialized", ctx.system->IsInitialized());
	SetField(L, "running", ctx.system->IsRunning());
	SetField(L, "healthy", ctx.system->IsHealthy());
	SetField(L, "fixed_delta_time", ctx.system->GetFixedDeltaTime());
	SetField(L, "is_physics_thread", ctx.IsPhysicsThread());
	return 1;
}

int LuaGetCoreSlots(lua_State* L) {
	BindingContext ctx = GetContext(L);
	lua_newtable(L);
	SetField(L, "system", ctx.system != nullptr);
	SetField(L, "thread", ctx.thread != nullptr);
	SetField(L, "world", ctx.world != nullptr);
	SetField(L, "script_vm", ctx.script_vm != nullptr);
	SetField(L, "all", ctx.script_vm != nullptr && ctx.script_vm->AreCoreSlotsValid());
	return 1;
}

int LuaGetThreadInfo(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_newtable(L);
	SetField(L, "running", ctx.thread != nullptr && ctx.thread->IsRunning());
	SetField(L, "healthy", ctx.thread != nullptr && ctx.thread->IsHealthy());
	SetField(L, "is_physics_thread", ctx.IsPhysicsThread());
	SetField(L, "logger_available", ctx.thread != nullptr && ctx.thread->GetLogger() != nullptr);
	SetField(L, "world_available", ctx.world != nullptr);
	SetField(L, "script_vm_available", ctx.script_vm != nullptr);
	return 1;
}

int LuaReloadThresholds(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	if (!ctx.system->ReloadThresholds()) {
		return PushNilError(L, "reload_thresholds failed");
	}
	lua_pushboolean(L, 1);
	return 1;
}

int LuaReloadLogLevel(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	if (!ctx.system->ReloadLogLevel()) {
		return PushNilError(L, "reload_log_level failed");
	}
	lua_pushboolean(L, 1);
	return 1;
}

int LuaGetPhysicsConfig(lua_State* L) {
	return PushConfigSnapshot<PhysicsConfig>(L,
											 &PhysicsSystem::GetPhysicsConfigSnapshot,
											 PushPhysicsConfig,
											 "physics config not available");
}

int LuaGetThreadingConfig(lua_State* L) {
	return PushConfigSnapshot<ThreadingConfig>(L,
											   &PhysicsSystem::GetThreadingConfigSnapshot,
											   PushThreadingConfig,
											   "threading config not available");
}

int LuaGetLogConfig(lua_State* L) {
	return PushConfigSnapshot<PhysicsLogConfig>(
		L, &PhysicsSystem::GetLogConfigSnapshot, PushLogConfig, "log config not available");
}

int LuaGetThresholdsConfig(lua_State* L) {
	return PushConfigSnapshot<ThresholdsConfig>(L,
												&PhysicsSystem::GetThresholdsConfigSnapshot,
												PushThresholdsConfig,
												"thresholds config not available");
}

int LuaGetConfig(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	auto physics = ctx.system->GetPhysicsConfigSnapshot();
	auto threading = ctx.system->GetThreadingConfigSnapshot();
	auto log = ctx.system->GetLogConfigSnapshot();
	auto thresholds = ctx.system->GetThresholdsConfigSnapshot();
	if (!physics || !threading || !log || !thresholds) {
		return PushNilError(L, "physics config not available");
	}

	lua_newtable(L);
	PushPhysicsConfig(L, *physics);
	lua_setfield(L, -2, "physics");
	PushThreadingConfig(L, *threading);
	lua_setfield(L, -2, "threading");
	PushLogConfig(L, *log);
	lua_setfield(L, -2, "log");
	PushThresholdsConfig(L, *thresholds);
	lua_setfield(L, -2, "thresholds");
	return 1;
}

int LuaDumpConfig(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	std::string dump = ctx.system->DumpConfig();
	if (dump.empty()) {
		return PushNilError(L, "physics config not available");
	}
	lua_pushlstring(L, dump.data(), dump.size());
	return 1;
}

int LuaGetPaths(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	lua_newtable(L);
	SetField(L, "config_dir", ctx.system->GetConfigDirSnapshot());
	SetField(L, "configDir", ctx.system->GetConfigDirSnapshot());
	SetField(L, "assets_path", ctx.system->GetAssetsPathSnapshot());
	SetField(L, "assetsPath", ctx.system->GetAssetsPathSnapshot());
	SetField(L, "scripts_dir", ctx.system->GetScriptsDirSnapshot());
	SetField(L, "scriptsDir", ctx.system->GetScriptsDirSnapshot());
	return 1;
}

const luaL_Reg kConfigFunctions[] = {{"is_initialized", LuaIsInitialized},
									 {"is_running", LuaIsRunning},
									 {"is_healthy", LuaIsHealthy},
									 {"status", LuaStatus},
									 {"get_core_slots", LuaGetCoreSlots},
									 {"get_thread_info", LuaGetThreadInfo},
									 {"get_fixed_delta_time", LuaGetFixedDeltaTime},
									 {"get_paths", LuaGetPaths},
									 {"reload_thresholds", LuaReloadThresholds},
									 {"reload_log_level", LuaReloadLogLevel},
									 {"get_config", LuaGetConfig},
									 {"get_physics_config", LuaGetPhysicsConfig},
									 {"get_threading_config", LuaGetThreadingConfig},
									 {"get_log_config", LuaGetLogConfig},
									 {"get_thresholds_config", LuaGetThresholdsConfig},
									 {"dump_config", LuaDumpConfig},
									 {nullptr, nullptr}};

}  // namespace

void RegisterConfigBindings(lua_State* L) {
	luaL_setfuncs(L, kConfigFunctions, 0);
}

void RegisterConstants(lua_State* L) {
	SetField(L, "COMMAND_SPAWN", "spawn");
	SetField(L, "COMMAND_DESTROY", "destroy");
	SetField(L, "COMMAND_APPLY_FORCE", "apply_force");
	SetField(L, "COMMAND_SET_VELOCITY", "set_velocity");
	SetField(L, "COMMAND_TICK", "tick");
	SetField(L, "COLLISION_START", "start");
	SetField(L, "COLLISION_PERSIST", "persist");
	SetField(L, "COLLISION_END", "end");
	SetField(L, "DIFF_POSITION", 1 << 0);
	SetField(L, "DIFF_ROTATION", 1 << 1);
	SetField(L, "DIFF_LINEAR_VELOCITY", 1 << 2);
	SetField(L, "DIFF_ANGULAR_VELOCITY", 1 << 3);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
