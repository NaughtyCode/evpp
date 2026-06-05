#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_system.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <new>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>

#include "runtime/physics/bind/physics_bindings.h"
#include "runtime/physics/physics_log.h"
#include "runtime/physics/physics_vm.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/bind/import_bind.h"
#include "runtime/script/bind/json_bind.h"
#include "runtime/core/timer/bind/timer_bind.h"
#include "runtime/vm/custom_ptr_store.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

namespace engine {

namespace {

bool IsFiniteReal(double value) {
	return std::isfinite(value) &&
		   std::abs(value) <= static_cast<double>((std::numeric_limits<JPH::Real>::max)());
}

bool IsFiniteRealVec3(double x, double y, double z) {
	return IsFiniteReal(x) && IsFiniteReal(y) && IsFiniteReal(z);
}

bool IsFiniteVec3(float x, float y, float z) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

bool IsFiniteQuat(float x, float y, float z, float w) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}

JPH::Quat NormalizedOrIdentity(float x, float y, float z, float w) {
	JPH::Quat q(x, y, z, w);
	return q.LengthSq() > 1.0e-12f ? q.Normalized() : JPH::Quat::sIdentity();
}

}  // namespace

// Singleton

PhysicsSystem& PhysicsSystem::Instance() {
	// Jolt owns process-global registries and allocator hooks. The engine
	// performs explicit PhysicsSystem::Shutdown() during Engine::Cleanup();
	// letting the C++ static-destruction phase tear down the wrapper after
	// those globals have started unwinding can jump through stale Jolt state.
	alignas(PhysicsSystem) static std::byte storage[sizeof(PhysicsSystem)];
	static PhysicsSystem* instance = ::new (static_cast<void*>(storage)) PhysicsSystem();
	return *instance;
}

// Initialize — load configs + create ScriptVM + load scripts [D22]

bool PhysicsSystem::Initialize(const std::string& config_dir,
							   const std::string& scripts_dir) {
	ENGINE_PROFILE_SCOPE("engine.physics", "Initialize");
	if (is_initialized_) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: already initialized");
		return false;
	}

	config_dir_ = config_dir;
	scripts_dir_ = scripts_dir;

	// ── Load configs ──────────────────────────────────────────────────
	config_manager_ = std::make_unique<PhysicsConfigManager>();
	if (!config_manager_->Load(config_dir)) {
		ENGINE_LOG_ERROR(GetLogger(), "PhysicsSystem: config loading failed");
		config_manager_.reset();
		return false;
	}

	// Compute assets_path from PhysicsConfig.scene_path.
	// config_dir is <resource_dir>/physics/config; scene_path is relative to
	// resource_dir (e.g. "/physics/data/scene.json").
	{
		std::filesystem::path resource_dir =
			std::filesystem::path(config_dir).parent_path().parent_path();
		std::string scene_path_text = config_manager_->GetPhysicsConfig().scene_path;
		std::filesystem::path scene_path(scene_path_text);
		bool has_drive_prefix = scene_path_text.size() >= 2 && scene_path_text[1] == ':';
		bool is_unc_path = scene_path_text.rfind("\\\\", 0) == 0;
		if ((has_drive_prefix || is_unc_path) && scene_path.is_absolute()) {
			assets_path_ = scene_path.string();
		} else {
			std::string relative_scene_path = scene_path_text;
			while (!relative_scene_path.empty() &&
				   (relative_scene_path.front() == '/' || relative_scene_path.front() == '\\')) {
				relative_scene_path.erase(relative_scene_path.begin());
			}
			assets_path_ = (resource_dir / relative_scene_path).string();
		}
	}

	ENGINE_LOG_INFO(GetLogger(), "PhysicsSystem: configs loaded from [{}]", config_dir);

	// ── Create physics-dedicated ScriptVM with [physics_vm] log prefix ─
	script_vm_ = std::make_unique<PhysicsScriptVM>();
	script_vm_->SetImportPath(std::filesystem::path(scripts_dir).parent_path().string());

	// Register subsystem objects in the VM's custom-pointer store so they
	// can be retrieved from any lua_State* via typed accessors.
	// Must be done before scripts are loaded on the physics thread; physics
	// scripts may call physics APIs during top-level execution.
	InitCustomPtrStore();

	// Register physics API bindings
	physics_bindings::Register(*script_vm_);

	// Register Glaze JSON bindings for physics scripts.
	script::ExportJson(*script_vm_);

	// Register per-thread timer API (timer.timeout / interval / cancel)
	physics_thread_.InitTimerManager();
	script::ExportTimer(*script_vm_, physics_thread_.GetTimerManager());

	// Register import() — physics scripts use import("runtime.common.class")
	ExportImport(*script_vm_);

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

	// Register before launching PT so the callback is immutable from the
	// physics thread's point of view.
	physics_thread_.SetPostStepCallback(
		[this](const std::vector<CollisionEvent>& events) { this->UpdateScript(events); });
	physics_thread_.SetStartupCallback([this]() {
		if (!script_vm_ || scripts_dir_.empty()) {
			return true;
		}

		size_t failed = script_vm_->DoDirectory(scripts_dir_);
		if (failed > 0) {
			ENGINE_LOG_ERROR(GetLogger(),
							 "PhysicsSystem: [{}] script(s) failed to load from [{}]",
							 failed,
							 scripts_dir_);
			return false;
		}
		script_vm_->InitScript();
		return true;
	});
	physics_thread_.SetShutdownCallback([this]() {
		if (!script_vm_) {
			return;
		}
		script_vm_->DestroyScript();
		script::ShutdownTimerBindings(*script_vm_);
	});

	bool ok = physics_thread_.Start(config_manager_->GetPhysicsConfig(),
									config_manager_->GetThreadingConfig(),
									config_manager_->GetThresholdsConfig(),
									config_manager_->GetLogConfig(),
									assets_path_);

	if (!ok) {
		ENGINE_LOG_ERROR(GetLogger(), "PhysicsSystem: failed to start physics thread");
		return false;
	}

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

	physics_thread_.SetPostStepCallback({});
	physics_thread_.SetStartupCallback({});
	physics_thread_.SetShutdownCallback({});

	if (script_vm_) {
		script::ShutdownTimerBindings(*script_vm_);
		script_vm_.reset();
	}

	config_manager_.reset();
	pending_results_.clear();
	is_initialized_ = false;

	ENGINE_LOG_INFO(GetLogger(), "PhysicsSystem: shutdown complete");
}

// IsRunning

bool PhysicsSystem::IsRunning() const {
	return is_initialized_ && physics_thread_.IsRunning();
}

// Enqueue commands (5 types)

bool PhysicsSystem::EnqueueSpawn(const std::string& proto_id,
								 double x,
								 double y,
								 double z,
								 float qx,
								 float qy,
								 float qz,
								 float qw,
								 uint64_t user_data) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueSpawn");
	if (!is_initialized_ || proto_id.empty() || !IsFiniteRealVec3(x, y, z) ||
		!IsFiniteQuat(qx, qy, qz, qw)) {
		return false;
	}
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: EnqueueSpawn called from physics thread");
		assert(false && "PhysicsSystem::EnqueueSpawn called from physics thread");
		return false;
	}
	SpawnArgs args;
	args.proto_id = proto_id;
	args.position = JPH::RVec3(
		static_cast<JPH::Real>(x), static_cast<JPH::Real>(y), static_cast<JPH::Real>(z));
	args.rotation = NormalizedOrIdentity(qx, qy, qz, qw);
	args.user_data = user_data;
	return physics_thread_.EnqueueCommand(PhysicsCommand::MakeSpawn(std::move(args)));
}

bool PhysicsSystem::EnqueueDestroy(uint32_t body_id) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueDestroy");
	if (!is_initialized_) return false;
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: EnqueueDestroy called from physics thread");
		assert(false && "PhysicsSystem::EnqueueDestroy called from physics thread");
		return false;
	}
	DestroyArgs args;
	args.body_id = body_id;
	return physics_thread_.EnqueueCommand(PhysicsCommand::MakeDestroy(args));
}

bool PhysicsSystem::EnqueueApplyForce(
	uint32_t body_id, float fx, float fy, float fz, double px, double py, double pz) {
	if (!is_initialized_ || !IsFiniteVec3(fx, fy, fz) || !IsFiniteRealVec3(px, py, pz)) {
		return false;
	}
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: EnqueueApplyForce called from physics thread");
		assert(false && "PhysicsSystem::EnqueueApplyForce called from physics thread");
		return false;
	}
	ApplyForceArgs args;
	args.body_id = body_id;
	args.force = JPH::Vec3(fx, fy, fz);
	args.point = JPH::RVec3(
		static_cast<JPH::Real>(px), static_cast<JPH::Real>(py), static_cast<JPH::Real>(pz));
	return physics_thread_.EnqueueCommand(PhysicsCommand::MakeApplyForce(std::move(args)));
}

bool PhysicsSystem::EnqueueSetVelocity(uint32_t body_id, float vx, float vy, float vz) {
	if (!is_initialized_ || !IsFiniteVec3(vx, vy, vz)) {
		return false;
	}
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: EnqueueSetVelocity called from physics thread");
		assert(false && "PhysicsSystem::EnqueueSetVelocity called from physics thread");
		return false;
	}
	SetVelocityArgs args;
	args.body_id = body_id;
	args.velocity = JPH::Vec3(vx, vy, vz);
	return physics_thread_.EnqueueCommand(PhysicsCommand::MakeSetVelocity(std::move(args)));
}

bool PhysicsSystem::Tick(uint64_t frame_id, float delta_time) {
	ENGINE_PROFILE_SCOPE("engine.physics", "EnqueueTick");
	if (!is_initialized_ || !std::isfinite(delta_time) || delta_time <= 0.0f) {
		return false;
	}
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: Tick called from physics thread");
		assert(false && "PhysicsSystem::Tick called from physics thread");
		return false;
	}
	TickArgs args;
	args.frame_id = frame_id;
	args.delta_time = delta_time;
	return physics_thread_.EnqueueCommand(PhysicsCommand::MakeTick(std::move(args)));
}

// FetchResult — blocking wait for a frame result

std::optional<PhysicsFrameResult> PhysicsSystem::FetchResult(uint64_t frame_id, int timeout_ms) {
	ENGINE_PROFILE_PHYSICS_FETCH(frame_id);
	if (!is_initialized_ || timeout_ms < 0) {
		return std::nullopt;
	}
	auto cached = pending_results_.find(frame_id);
	if (cached != pending_results_.end()) {
		PhysicsFrameResult result = std::move(cached->second);
		pending_results_.erase(cached);
		return result;
	}

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

	while (true) {
		auto result = physics_thread_.TryDequeueResult();
		if (result) {
			if (result->frame_id == frame_id) {
				return std::move(*result);
			}
			StorePendingResult(std::move(*result));
			continue;
		}

		auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return std::nullopt;
		}

		physics_thread_.WaitForResult(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
	}
}

void PhysicsSystem::StorePendingResult(PhysicsFrameResult&& result) {
	size_t limit = 64;
	if (config_manager_) {
		limit = static_cast<size_t>(
			std::max(1, config_manager_->GetThreadingConfig().result_queue_size));
	}

	if (pending_results_.size() >= limit) {
		auto oldest = pending_results_.begin();
		for (auto it = pending_results_.begin(); it != pending_results_.end(); ++it) {
			if (it->first < oldest->first) {
				oldest = it;
			}
		}
		pending_results_.erase(oldest);
	}

	pending_results_[result.frame_id] = std::move(result);
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
	if (!config_manager_->ReloadLogLevel(config_dir_)) return false;
	physics_thread_.SetLogLevel(config_manager_->GetLogConfig().level);
	return true;
}

// Synchronous query methods — thread-safe via Jolt BodyLockInterface

std::optional<BodyTransform> PhysicsSystem::GetTransform(uint32_t body_id) const {
	ENGINE_PROFILE_SCOPE("engine.physics", "GetTransform");
	if (!is_initialized_ || !physics_thread_.IsRunning()) return std::nullopt;
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
	if (!is_initialized_ || !physics_thread_.IsRunning()) return std::nullopt;
	auto vel = physics_thread_.GetWorld().GetVelocity(body_id);
	if (!vel.has_value()) return std::nullopt;
	return Vec3Result{vel->GetX(), vel->GetY(), vel->GetZ()};
}

bool PhysicsSystem::IsBodyActive(uint32_t body_id) const {
	if (!is_initialized_ || !physics_thread_.IsRunning()) return false;
	return physics_thread_.GetWorld().IsActive(body_id);
}

std::optional<PhysicsSystem::RayCastResult> PhysicsSystem::RayCast(
	double ox, double oy, double oz, double dx, double dy, double dz, float max_dist) const {
	if (!is_initialized_ || !physics_thread_.IsRunning() || !IsFiniteRealVec3(ox, oy, oz)) {
		return std::nullopt;
	}
	auto hit = physics_thread_.GetWorld().RayCast(
		JPH::RVec3(
			static_cast<JPH::Real>(ox), static_cast<JPH::Real>(oy), static_cast<JPH::Real>(oz)),
		JPH::Vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)),
		max_dist);
	if (!hit.has_value()) return std::nullopt;
	return RayCastResult{hit->body_id, hit->x, hit->y, hit->z};
}

PhysicsSystem::PhysicsStats PhysicsSystem::GetPhysicsStats() const {
	if (!is_initialized_ || !physics_thread_.IsRunning()) return {};
	auto s = physics_thread_.GetWorld().GetStats();
	return {s.active_bodies, s.total_bodies, s.body_pairs, s.contact_constraints};
}

std::optional<PhysicsConfig> PhysicsSystem::GetPhysicsConfigSnapshot() const {
	if (!config_manager_) return std::nullopt;
	return config_manager_->GetPhysicsConfig();
}

std::optional<ThreadingConfig> PhysicsSystem::GetThreadingConfigSnapshot() const {
	if (!config_manager_) return std::nullopt;
	return config_manager_->GetThreadingConfig();
}

std::optional<PhysicsLogConfig> PhysicsSystem::GetLogConfigSnapshot() const {
	if (!config_manager_) return std::nullopt;
	return config_manager_->GetLogConfig();
}

std::optional<ThresholdsConfig> PhysicsSystem::GetThresholdsConfigSnapshot() const {
	if (!config_manager_) return std::nullopt;
	return config_manager_->GetThresholdsConfig();
}

std::string PhysicsSystem::DumpConfig() const {
	if (!config_manager_) return {};
	return config_manager_->Dump();
}

// SaveState / RestoreState

std::string PhysicsSystem::SaveState() const {
	if (!is_initialized_ || !physics_thread_.IsRunning() || !physics_thread_.IsPhysicsThread()) {
		return {};
	}
	return physics_thread_.SaveState();
}

bool PhysicsSystem::RestoreState(const std::string& data) {
	if (!is_initialized_ || !physics_thread_.IsRunning() || !physics_thread_.IsPhysicsThread()) {
		return false;
	}
	return physics_thread_.RestoreState(data);
}

bool PhysicsSystem::Recover(const std::string& saved_state) {
	if (!is_initialized_) {
		ENGINE_LOG_WARN(GetLogger(), "PhysicsSystem: not initialized, cannot recover");
		return false;
	}
	if (physics_thread_.IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(physics_thread_.GetLogger(),
						  "PhysicsSystem: recover cannot be called from physics Lua");
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

	for (const auto& evt : collision_events) {
		const int base = lua_gettop(L);

		lua_getglobal(L, "on_physics_collision");
		if (!lua_isfunction(L, -1)) {
			lua_settop(L, base);
			return;
		}

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
			lua_settop(L, base);
			continue;
		}
		lua_remove(L, msgh);
		lua_settop(L, base);
	}
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
