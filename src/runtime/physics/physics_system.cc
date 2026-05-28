#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_system.h"

#include <chrono>
#include <cstdio>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

#include "runtime/physics/physics_bindings.h"
#include "runtime/physics/physics_log.h"
#include "runtime/physics/physics_vm.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/import_bind.h"
#include "runtime/vm/custom_ptr_store.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

namespace engine {

// Singleton

PhysicsSystem& PhysicsSystem::Instance() {
	static PhysicsSystem instance;
	return instance;
}

// Initialize — load configs + create ScriptVM + load scripts [D22]

bool PhysicsSystem::Initialize(const std::string& config_dir,
							   const std::string& assets_path,
							   const std::string& scripts_dir) {
	ENGINE_PROFILE_SCOPE("engine.physics", "Initialize");
	if (is_initialized_) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: already initialized");
		return false;
	}

	config_dir_ = config_dir;
	assets_path_ = assets_path;
	scripts_dir_ = scripts_dir;

	// ── Load configs ──────────────────────────────────────────────────
	config_manager_ = std::make_unique<PhysicsConfigManager>();
	if (!config_manager_->Load(config_dir)) {
		ENGINE_LOG_ERROR(GetLogger(), "PhysicsSystem: config loading failed");
		config_manager_.reset();
		return false;
	}

	ENGINE_LOG_INFO(GetLogger(), "PhysicsSystem: configs loaded from [{}]", config_dir);

	// ── Create physics-dedicated ScriptVM with [physics_vm] log prefix ─
	script_vm_ = std::make_unique<PhysicsScriptVM>();
	script_vm_->SetImportPath(std::filesystem::path(scripts_dir).parent_path().string());

	// Register subsystem objects in the VM's custom-pointer store so they
	// can be retrieved from any lua_State* via typed accessors.
	// Must be done BEFORE DoDirectory — physics scripts may call physics
	// APIs during top-level execution.
	InitCustomPtrStore();

	// Register physics API bindings
	physics_bindings::Register(*script_vm_);

	// Register import() — physics scripts use import("runtime.common.class")
	ExportImport(*script_vm_);

	// Load physics scripts
	if (!scripts_dir.empty()) {
		size_t failed = script_vm_->DoDirectory(scripts_dir);
		if (failed > 0) {
			ENGINE_LOG_WARN(GetLogger(),
							"PhysicsSystem: [{}] script(s) failed to load from [{}]",
							failed,
							scripts_dir);
		}
		script_vm_->InitScript();
	}

	is_initialized_ = true;

	ENGINE_LOG_INFO(GetLogger(), "PhysicsSystem: initialized (physics thread NOT started)");
	return true;
}

// InitCustomPtrStore — register subsystem objects in the VM [custom ptr array]

void PhysicsSystem::InitCustomPtrStore() {
	if (!script_vm_) return;
	script_vm_->RegisterSubsystemObjects(this, &physics_thread_, &physics_thread_.GetWorld());
	// Note: RegisterSubsystemObjects handles Reserve(4) internally.
}

// Typed accessors — retrieve subsystem objects from any physics lua_State

PhysicsSystem* PhysicsSystem::GetSystemFromState(lua_State* L) {
	VMCustomPtrStore store(L);
	return store.GetAs<PhysicsSystem>(kPhysPtrSystem);
}

PhysicsThread* PhysicsSystem::GetThreadFromState(lua_State* L) {
	VMCustomPtrStore store(L);
	return store.GetAs<PhysicsThread>(kPhysPtrThread);
}

PhysicsWorld* PhysicsSystem::GetWorldFromState(lua_State* L) {
	VMCustomPtrStore store(L);
	return store.GetAs<PhysicsWorld>(kPhysPtrWorld);
}

PhysicsScriptVM* PhysicsSystem::GetScriptVMFromState(lua_State* L) {
	VMCustomPtrStore store(L);
	return store.GetAs<PhysicsScriptVM>(kPhysPtrScriptVM);
}

// Start — explicitly start the physics thread

bool PhysicsSystem::Start() {
	ENGINE_PROFILE_SCOPE("engine.physics", "Start");
	if (!is_initialized_) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: not initialized, cannot start");
		return false;
	}
	if (physics_thread_.IsRunning()) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: physics thread already running");
		return false;
	}

	bool ok = physics_thread_.Start(config_manager_->GetPhysicsConfig(),
									config_manager_->GetThreadingConfig(),
									config_manager_->GetThresholdsConfig(),
									config_manager_->GetLogConfig(),
									assets_path_);

	if (!ok) {
		ENGINE_LOG_ERROR(GetLogger(), "PhysicsSystem: failed to start physics thread");
		return false;
	}

	// Register post-step callback — drives Lua collision callbacks on the
	// physics thread after each world_.Step().
	physics_thread_.SetPostStepCallback(
		[this](const std::vector<CollisionEvent>& events) { this->UpdateScript(events); });

	PHYSICS_LOG_INFO(physics_thread_.GetLogger(), "PhysicsSystem: physics thread started");
	return true;
}

// Shutdown

void PhysicsSystem::Shutdown() {
	ENGINE_PROFILE_SCOPE("engine.physics", "Shutdown");
	if (physics_thread_.IsRunning()) {
		PHYSICS_LOG_INFO(physics_thread_.GetLogger(), "PhysicsSystem: shutdown commencing...");
		physics_thread_.Stop();
	}

	if (script_vm_) {
		script_vm_->DestroyScript();
		script_vm_.reset();
	}

	config_manager_.reset();
	is_initialized_ = false;

	ENGINE_LOG_INFO(GetLogger(), "PhysicsSystem: shutdown complete");
}

// IsRunning

bool PhysicsSystem::IsRunning() const {
	return is_initialized_ && physics_thread_.IsRunning();
}

// Enqueue commands (5 types)

void PhysicsSystem::EnqueueSpawn(const std::string& proto_id,
								 double x,
								 double y,
								 double z,
								 float qx,
								 float qy,
								 float qz,
								 float qw,
								 uint64_t user_data) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueSpawn");
	SpawnArgs args;
	args.proto_id = proto_id;
	args.position = JPH::RVec3(x, y, z);
	args.rotation = JPH::Quat(qx, qy, qz, qw);
	args.user_data = user_data;
	physics_thread_.EnqueueCommand(PhysicsCommand::MakeSpawn(std::move(args)));
}

void PhysicsSystem::EnqueueDestroy(uint32_t body_id) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueDestroy");
	DestroyArgs args;
	args.body_id = body_id;
	physics_thread_.EnqueueCommand(PhysicsCommand::MakeDestroy(args));
}

void PhysicsSystem::EnqueueApplyForce(
	uint32_t body_id, float fx, float fy, float fz, double px, double py, double pz) {
	ApplyForceArgs args;
	args.body_id = body_id;
	args.force = JPH::Vec3(fx, fy, fz);
	args.point = JPH::RVec3(px, py, pz);
	physics_thread_.EnqueueCommand(PhysicsCommand::MakeApplyForce(std::move(args)));
}

void PhysicsSystem::EnqueueSetVelocity(uint32_t body_id, float vx, float vy, float vz) {
	SetVelocityArgs args;
	args.body_id = body_id;
	args.velocity = JPH::Vec3(vx, vy, vz);
	physics_thread_.EnqueueCommand(PhysicsCommand::MakeSetVelocity(std::move(args)));
}

void PhysicsSystem::Tick(uint64_t frame_id, float delta_time) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueTick");
	TickArgs args;
	args.frame_id = frame_id;
	args.delta_time = delta_time;
	physics_thread_.EnqueueCommand(PhysicsCommand::MakeTick(std::move(args)));
}

// FetchResult — blocking wait for a frame result

std::optional<PhysicsFrameResult> PhysicsSystem::FetchResult(uint64_t frame_id, int timeout_ms) {
	ENGINE_PROFILE_PHYSICS_FETCH(frame_id);
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

	while (true) {
		auto result = physics_thread_.TryDequeueResult();
		if (result) {
			if (result->frame_id == frame_id) {
				return std::move(*result);
			}
		}

		auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return std::nullopt;
		}

		physics_thread_.WaitForResult(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
	}
}

// IsHealthy

bool PhysicsSystem::IsHealthy() const {
	return physics_thread_.IsHealthy();
}

// Config hot-reload

bool PhysicsSystem::ReloadThresholds() {
	if (!config_manager_) return false;
	if (!config_manager_->ReloadThresholds(config_dir_)) return false;
	// Propagate to running physics thread
	if (physics_thread_.IsRunning()) {
		physics_thread_.SetThresholds(config_manager_->GetThresholdsConfig());
	}
	return true;
}

bool PhysicsSystem::ReloadLogLevel() {
	if (!config_manager_) return false;
	return config_manager_->ReloadLogLevel(config_dir_);
}

// Synchronous query methods — thread-safe via Jolt BodyLockInterface

std::optional<BodyTransform> PhysicsSystem::GetTransform(uint32_t body_id) const {
	ENGINE_PROFILE_SCOPE("engine.physics", "GetTransform");
	if (!is_initialized_) return std::nullopt;
	auto result = physics_thread_.GetWorld().GetTransform(body_id);
	if (!result.has_value()) return std::nullopt;
	BodyTransform bt;
	bt.body_id = body_id;
	bt.pos_x = result->first.GetX();
	bt.pos_y = result->first.GetY();
	bt.pos_z = result->first.GetZ();
	bt.rot_x = result->second.GetX();
	bt.rot_y = result->second.GetY();
	bt.rot_z = result->second.GetZ();
	bt.rot_w = result->second.GetW();
	return bt;
}

std::optional<PhysicsSystem::Vec3Result> PhysicsSystem::GetVelocity(uint32_t body_id) const {
	if (!is_initialized_) return std::nullopt;
	auto vel = physics_thread_.GetWorld().GetVelocity(body_id);
	if (!vel.has_value()) return std::nullopt;
	return Vec3Result{vel->GetX(), vel->GetY(), vel->GetZ()};
}

bool PhysicsSystem::IsBodyActive(uint32_t body_id) const {
	if (!is_initialized_) return false;
	return physics_thread_.GetWorld().IsActive(body_id);
}

std::optional<PhysicsSystem::RayCastResult> PhysicsSystem::RayCast(
	double ox, double oy, double oz, double dx, double dy, double dz, float max_dist) const {
	if (!is_initialized_) return std::nullopt;
	auto hit = physics_thread_.GetWorld().RayCast(
		JPH::RVec3(ox, oy, oz),
		JPH::Vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)),
		max_dist);
	if (!hit.has_value()) return std::nullopt;
	return RayCastResult{hit->body_id, hit->x, hit->y, hit->z};
}

PhysicsSystem::PhysicsStats PhysicsSystem::GetPhysicsStats() const {
	if (!is_initialized_) return {};
	auto s = physics_thread_.GetWorld().GetStats();
	return {s.active_bodies, s.total_bodies, s.body_pairs, s.contact_constraints};
}

// SaveState / RestoreState

std::string PhysicsSystem::SaveState() const {
	if (!is_initialized_) return {};
	return physics_thread_.SaveState();
}

bool PhysicsSystem::RestoreState(const std::string& data) {
	if (!is_initialized_) return false;
	return physics_thread_.RestoreState(data);
}

bool PhysicsSystem::Recover(const std::string& saved_state) {
	if (!is_initialized_) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: not initialized, cannot recover");
		return false;
	}
	return physics_thread_.Recover(saved_state);
}

// UpdateScript — call Lua collision callbacks [D17.6]
//
// PT-only. Verified at entry by physics_thread_.VerifyIsPhysicsThread().

void PhysicsSystem::UpdateScript(const std::vector<CollisionEvent>& collision_events) {
	ENGINE_PROFILE_SCRIPT_CALLBACK();
	// This method must only execute on the physics thread. It is invoked
	// via the PostStepCallback inside PhysicsThread::EventLoop(). Calling
	// it from any other thread (e.g. the main thread) would race with the
	// physics thread's exclusive access to script_vm_'s Lua state.
	physics_thread_.VerifyIsPhysicsThread();

	if (!script_vm_) return;

	// Drive Lua coroutines
	script_vm_->UpdateScript();

	if (collision_events.empty()) return;

	lua_State* L = script_vm_->GetState();
	if (!L) return;

	// Look up on_physics_collision in Lua globals
	lua_getglobal(L, "on_physics_collision");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	for (const auto& evt : collision_events) {
		// Push event table for each collision
		lua_newtable(L);

		lua_pushinteger(L, evt.body_a);
		lua_setfield(L, -2, "body_a");

		lua_pushinteger(L, evt.body_b);
		lua_setfield(L, -2, "body_b");

		const char* type_str = "start";
		if (evt.type == CollisionEvent::Type::Persist)
			type_str = "persist";
		else if (evt.type == CollisionEvent::Type::End)
			type_str = "end";
		lua_pushstring(L, type_str);
		lua_setfield(L, -2, "type");

		// Contact points array
		lua_newtable(L);
		for (size_t i = 0; i < evt.contact_points.size(); ++i) {
			lua_newtable(L);
			lua_pushnumber(L, evt.contact_points[i].GetX());
			lua_setfield(L, -2, "x");
			lua_pushnumber(L, evt.contact_points[i].GetY());
			lua_setfield(L, -2, "y");
			lua_pushnumber(L, evt.contact_points[i].GetZ());
			lua_setfield(L, -2, "z");
			lua_rawseti(L, -2, static_cast<int>(i + 1));
		}
		lua_setfield(L, -2, "points");

		// Call on_physics_collision(event)
		int msgh = PushLuaErrorHandlerForCall(L, 1);
		if (lua_pcall(L, 1, 0, msgh) != LUA_OK) {
			PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
							  "PhysicsSystem: on_physics_collision error: {}",
							  lua_tostring(L, -1));
			lua_pop(L, 1);
		}

		// Re-fetch function for next call
		lua_getglobal(L, "on_physics_collision");
	}

	lua_pop(L, 1);	// pop the function
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
