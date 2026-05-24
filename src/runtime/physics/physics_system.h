#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "runtime/physics/physics_config.h"
#include "runtime/physics/physics_commands.h"
#include "runtime/physics/physics_thread.h"
#include "runtime/physics/physics_vm.h"

namespace engine {

//============================================================================
// PhysicsSystem — singleton facade that owns the complete physics subsystem
//
// Lifecycle:
//   1. Initialize()  — loads config + creates ScriptVM + loads scripts
//                     (does NOT start physics thread)
//   2. Start()       — starts physics thread (physics simulation begins)
//   3. Shutdown()    — stops physics thread + destroys VM
//
// Design doc §3.2 interface: 5 command enqueue methods + FetchResult
//============================================================================

class PhysicsSystem {
public:
    static PhysicsSystem& Instance();

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    // ── Lifecycle ───────────────────────────────────────────────────────

    // Load configs, create ScriptVM, load scripts. Does NOT start physics.
    // Returns false if config loading or VM initialization fails [D22].
    bool Initialize(const std::string& config_dir,
                    const std::string& assets_path,
                    const std::string& scripts_dir);

    // Explicitly start the physics thread. Must be called after Initialize().
    // Returns false if already running or not initialized.
    bool Start();

    // Stop physics thread + destroy ScriptVM.
    void Shutdown();

    bool IsRunning() const;
    bool IsInitialized() const { return is_initialized_; }

    // ── Command enqueue (5 commands per [D3]) ──────────────────────────

    void EnqueueSpawn(const std::string& proto_id,
                      double x, double y, double z,
                      float qx, float qy, float qz, float qw,
                      uint64_t user_data = 0);
    void EnqueueDestroy(uint32_t body_id);
    void EnqueueApplyForce(uint32_t body_id,
                           float fx, float fy, float fz,
                           double px, double py, double pz);
    void EnqueueSetVelocity(uint32_t body_id, float vx, float vy, float vz);
    void Tick(uint64_t frame_id, float delta_time);

    // ── Result retrieval ────────────────────────────────────────────────
    std::optional<PhysicsFrameResult> FetchResult(uint64_t frame_id,
                                                   int timeout_ms);
    bool IsHealthy() const;

    // ── Save / Restore / Recovery ──────────────────────────────────────
    std::string SaveState() const;
    bool RestoreState(const std::string& data);
    // Recover after physics thread crash. Optionally restores from
    // a previously saved state blob. Returns false on failure.
    bool Recover(const std::string& saved_state = {});

    // ── Config hot-reload ──────────────────────────────────────────────
    bool ReloadThresholds();
    bool ReloadLogLevel();

    // ── Synchronous queries (thread-safe via Jolt BodyLockInterface) ────
    std::optional<BodyTransform> GetTransform(uint32_t body_id) const;
    struct Vec3Result { float x = 0, y = 0, z = 0; };
    std::optional<Vec3Result> GetVelocity(uint32_t body_id) const;
    bool IsBodyActive(uint32_t body_id) const;

    struct RayCastResult {
        uint32_t body_id = 0;
        double x = 0, y = 0, z = 0;
    };
    std::optional<RayCastResult> RayCast(double ox, double oy, double oz,
                                          double dx, double dy, double dz,
                                          float max_dist) const;

    struct PhysicsStats {
        uint32_t active_bodies = 0;
        uint32_t total_bodies = 0;
        int body_pairs = 0;
        int contact_constraints = 0;
    };
    PhysicsStats GetPhysicsStats() const;

    // ── ScriptVM access ─────────────────────────────────────────────────
    ScriptVM& GetScriptVM() { return *script_vm_; }
    const ScriptVM& GetScriptVM() const { return *script_vm_; }

    PhysicsScriptVM& GetPhysicsScriptVM() { return *script_vm_; }
    const PhysicsScriptVM& GetPhysicsScriptVM() const { return *script_vm_; }

    // ── Lua script update (main thread, after FetchResult) ─────────────
    void UpdateScript();

    // ── Fixed delta time (for Engine FrameLoop Tick) ───────────────────
    float GetFixedDeltaTime() const {
        return config_manager_ ? config_manager_->GetPhysicsConfig().fixed_delta_time
                              : 0.01667f;
    }

private:
    PhysicsSystem() = default;
    ~PhysicsSystem() = default;

    // Cached collision events from most recent FetchResult, for UpdateScript
    std::vector<CollisionEvent> last_collision_events_;
    std::mutex collision_events_mutex_;

    std::unique_ptr<PhysicsConfigManager> config_manager_;
    std::unique_ptr<PhysicsScriptVM> script_vm_;
    PhysicsThread physics_thread_;

    std::string config_dir_;
    std::string assets_path_;
    std::string scripts_dir_;

    bool is_initialized_ = false;
};

// Forward declare bindings registration (implemented in physics_bindings.cc)
namespace physics_bindings {
    void Register(ScriptVM& vm);
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
