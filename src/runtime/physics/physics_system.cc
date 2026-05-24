#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_system.h"

#include <cstdio>
#include <chrono>
#include <thread>

#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/vm/vm.h"

namespace engine {

//============================================================================
// Singleton
//============================================================================

PhysicsSystem& PhysicsSystem::Instance() {
    static PhysicsSystem instance;
    return instance;
}

//============================================================================
// Initialize — load configs + create ScriptVM + load scripts [D22]
//============================================================================

bool PhysicsSystem::Initialize(const std::string& config_dir,
                                const std::string& assets_path,
                                const std::string& scripts_dir) {
    if (is_initialized_) {
        std::fprintf(stderr, "PhysicsSystem: already initialized\n");
        return false;
    }

    config_dir_ = config_dir;
    assets_path_ = assets_path;
    scripts_dir_ = scripts_dir;

    // ── Load configs ──────────────────────────────────────────────────
    config_manager_ = std::make_unique<PhysicsConfigManager>();
    if (!config_manager_->Load(config_dir)) {
        std::fprintf(stderr, "PhysicsSystem: config loading failed\n");
        config_manager_.reset();
        return false;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "PhysicsSystem: configs loaded from [{}]", config_dir);

    // ── Create physics-dedicated ScriptVM ─────────────────────────────
    script_vm_ = std::make_unique<ScriptVM>();
    script_vm_->SetImportPath(scripts_dir);

    // Register physics API bindings
    physics_bindings::Register(*script_vm_);

    // Load physics scripts
    if (!scripts_dir.empty()) {
        size_t failed = script_vm_->DoDirectory(scripts_dir);
        if (failed > 0) {
            ENGINE_LOG_WARN(logger,
                "PhysicsSystem: [{}] script(s) failed to load from [{}]",
                failed, scripts_dir);
        }
        script_vm_->InitScript();
    }

    is_initialized_ = true;
    ENGINE_LOG_INFO(logger, "PhysicsSystem: initialized (physics thread NOT started)");
    return true;
}

//============================================================================
// Start — explicitly start the physics thread
//============================================================================

bool PhysicsSystem::Start() {
    if (!is_initialized_) {
        std::fprintf(stderr, "PhysicsSystem: not initialized, cannot start\n");
        return false;
    }
    if (physics_thread_.IsRunning()) {
        std::fprintf(stderr, "PhysicsSystem: physics thread already running\n");
        return false;
    }

    bool ok = physics_thread_.Start(
        config_manager_->GetPhysicsConfig(),
        config_manager_->GetThreadingConfig(),
        config_manager_->GetThresholdsConfig(),
        config_manager_->GetLogConfig(),
        assets_path_);

    if (!ok) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "PhysicsSystem: failed to start physics thread");
        return false;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "PhysicsSystem: physics thread started");
    return true;
}

//============================================================================
// Shutdown
//============================================================================

void PhysicsSystem::Shutdown() {
    if (physics_thread_.IsRunning()) {
        physics_thread_.Stop();
    }

    if (script_vm_) {
        script_vm_->DestroyScript();
        script_vm_.reset();
    }

    config_manager_.reset();
    is_initialized_ = false;

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "PhysicsSystem: shutdown complete");
}

//============================================================================
// IsRunning
//============================================================================

bool PhysicsSystem::IsRunning() const {
    return is_initialized_ && physics_thread_.IsRunning();
}

//============================================================================
// Enqueue commands (5 types)
//============================================================================

void PhysicsSystem::EnqueueSpawn(const std::string& proto_id,
                                  double x, double y, double z,
                                  float qx, float qy, float qz, float qw,
                                  uint64_t user_data) {
    SpawnArgs args;
    args.proto_id = proto_id;
    args.position = JPH::RVec3(x, y, z);
    args.rotation = JPH::Quat(qx, qy, qz, qw);
    args.user_data = user_data;
    physics_thread_.EnqueueCommand(PhysicsCommand::MakeSpawn(std::move(args)));
}

void PhysicsSystem::EnqueueDestroy(uint32_t body_id) {
    DestroyArgs args;
    args.body_id = body_id;
    physics_thread_.EnqueueCommand(PhysicsCommand::MakeDestroy(args));
}

void PhysicsSystem::EnqueueApplyForce(uint32_t body_id,
                                       float fx, float fy, float fz,
                                       double px, double py, double pz) {
    ApplyForceArgs args;
    args.body_id = body_id;
    args.force = JPH::Vec3(fx, fy, fz);
    args.point = JPH::RVec3(px, py, pz);
    physics_thread_.EnqueueCommand(PhysicsCommand::MakeApplyForce(std::move(args)));
}

void PhysicsSystem::EnqueueSetVelocity(uint32_t body_id,
                                        float vx, float vy, float vz) {
    SetVelocityArgs args;
    args.body_id = body_id;
    args.velocity = JPH::Vec3(vx, vy, vz);
    physics_thread_.EnqueueCommand(PhysicsCommand::MakeSetVelocity(std::move(args)));
}

void PhysicsSystem::Tick(uint64_t frame_id, float delta_time) {
    TickArgs args;
    args.frame_id = frame_id;
    args.delta_time = delta_time;
    physics_thread_.EnqueueCommand(PhysicsCommand::MakeTick(std::move(args)));
}

//============================================================================
// FetchResult — blocking wait for a frame result
//============================================================================

std::optional<PhysicsFrameResult> PhysicsSystem::FetchResult(
    uint64_t frame_id, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto result = physics_thread_.TryDequeueResult();
        if (result) {
            // Check frame_id match [D20]
            if (result->frame_id == frame_id) {
                // Cache collision events for UpdateScript
                {
                    std::lock_guard<std::mutex> lock(collision_events_mutex_);
                    last_collision_events_ = result->collision_events;
                }
                return std::move(*result);
            }
            // Mismatched frame — could be from a previous run; discard
            // (in practice shouldn't happen if Tick/Fetch are paired)
        }

        // Timeout check
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start).count();
        if (elapsed >= timeout_ms) {
            return std::nullopt;
        }

        // Brief sleep to avoid busy-wait
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

//============================================================================
// IsHealthy
//============================================================================

bool PhysicsSystem::IsHealthy() const {
    return physics_thread_.IsHealthy();
}

//============================================================================
// Config hot-reload
//============================================================================

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

//============================================================================
// Synchronous query methods — thread-safe via Jolt BodyLockInterface
//============================================================================

std::optional<BodyTransform> PhysicsSystem::GetTransform(uint32_t body_id) const {
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

std::optional<PhysicsSystem::Vec3Result> PhysicsSystem::GetVelocity(
    uint32_t body_id) const {
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
    double ox, double oy, double oz,
    double dx, double dy, double dz,
    float max_dist) const {
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

//============================================================================
// SaveState / RestoreState
//============================================================================

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
        std::fprintf(stderr, "PhysicsSystem: not initialized, cannot recover\n");
        return false;
    }
    return physics_thread_.Recover(saved_state);
}

//============================================================================
// UpdateScript — call Lua collision callbacks [D17.6]
//============================================================================

void PhysicsSystem::UpdateScript() {
    if (!script_vm_) return;

    // Drive Lua coroutines
    script_vm_->UpdateScript();

    // Fetch cached collision events
    std::vector<CollisionEvent> events;
    {
        std::lock_guard<std::mutex> lock(collision_events_mutex_);
        events = last_collision_events_;
    }

    if (events.empty()) return;

    lua_State* L = script_vm_->GetState();
    if (!L) return;

    // Look up on_physics_collision in Lua globals
    lua_getglobal(L, "on_physics_collision");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    for (const auto& evt : events) {
        // Push event table for each collision
        lua_newtable(L);

        lua_pushinteger(L, evt.body_a);
        lua_setfield(L, -2, "body_a");

        lua_pushinteger(L, evt.body_b);
        lua_setfield(L, -2, "body_b");

        const char* type_str = "start";
        if (evt.type == CollisionEvent::Type::Persist) type_str = "persist";
        else if (evt.type == CollisionEvent::Type::End) type_str = "end";
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
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            auto* logger = GetLogger();
            ENGINE_LOG_ERROR(logger, "PhysicsSystem: on_physics_collision error: {}",
                             lua_tostring(L, -1));
            lua_pop(L, 1);
        }

        // Re-fetch function for next call
        lua_getglobal(L, "on_physics_collision");
    }

    lua_pop(L, 1);  // pop the function
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
