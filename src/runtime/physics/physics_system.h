#pragma once

//==============================================================================
// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// physics_system.h is an internal implementation detail of the physics
// subsystem. External modules MUST NOT include this header directly.
// All external access must go through physics_engine_bridge.h.
//
// Physics-internal .cc files must define this macro before including:
//   #define PHYSICS_INTERNAL_ACCESS
//   #include "runtime/physics/physics_system.h"
//
// Including this header without the macro will cause a compile-time #error.
//==============================================================================
#ifndef PHYSICS_INTERNAL_ACCESS
#error "physics_system.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "runtime/physics/physics_config.h"
#include "runtime/physics/physics_commands.h"
#include "runtime/physics/physics_thread.h"
#include "runtime/physics/physics_vm.h"

namespace engine {

//==============================================================================
// PhysicsSystem — physics subsystem facade (singleton, internal implementation)
//
// [Thread Model]
//
//   PhysicsSystem does not have its own thread of execution. Its members are
//   distributed across two threads:
//
//   ┌──────────────────────┬──────────────────────┬──────────────────────────┐
//   │ Method               │ Calling thread       │ Safety mechanism         │
//   ├──────────────────────┼──────────────────────┼──────────────────────────┤
//   │ Initialize()         │ MT (main thread)     │ Lifecycle isolation      │
//   │ Start()              │ MT                   │ Launches PT              │
//   │ Shutdown()           │ MT                   │ Joins PT before cleanup  │
//   │ Tick()               │ MT                   │ SPSC lock-free enqueue   │
//   │ FetchResult()        │ MT                   │ SPSC lock-free dequeue   │
//   │ EnqueueSpawn()       │ MT                   │ SPSC lock-free enqueue   │
//   │ EnqueueDestroy()     │ MT                   │ SPSC lock-free enqueue   │
//   │ EnqueueApplyForce()  │ MT                   │ SPSC lock-free enqueue   │
//   │ EnqueueSetVelocity() │ MT                   │ SPSC lock-free enqueue   │
//   │ IsRunning()          │ MT / any             │ std::atomic<bool> read   │
//   │ IsHealthy()          │ MT / any             │ std::atomic<bool> read   │
//   │ GetTransform() etc.  │ MT                   │ Jolt BodyLockInterface   │
//   │ ReloadThresholds()   │ MT                   │ Via SetThresholds        │
//   │ UpdateScript()       │ PT (physics thread)  │ PT-exclusive, no lock    │
//   │ SaveState/Restore    │ MT                   │ Delegate to PhysicsThread│
//   └──────────────────────┴──────────────────────┴──────────────────────────┘
//
//   Key design rules:
//     1. All MT-side methods are called indirectly through PhysicsEngineBridge
//        (a friend class with direct access to PhysicsSystem private methods).
//     2. UpdateScript() runs exclusively on the physics thread (via the
//        PostStepCallback). It has exclusive access to script_vm_ and needs
//        no locking.
//     3. physics_bindings::Register() is called from Initialize() on the MT
//        while the physics thread has NOT yet started — no concurrency issue.
//     4. After Start(), the script_vm_ Lua state is exclusively accessed by
//        the PT. The MT never touches it.
//     5. All MT <-> PT communication goes through PhysicsThread's SPSC queues.
//        PhysicsSystem itself holds no mutexes.
//
// [External Module Access — FORBIDDEN]
//
//   External modules MUST NOT call PhysicsSystem::Instance() directly.
//   All external functionality is exposed through PhysicsEngineBridge
//   (friend class). Compile-time protection is provided by the
//   PHYSICS_INTERNAL_ACCESS macro at the top of this file.
//==============================================================================

class PhysicsSystem {
public:
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    // ==================================================================
    // Public interface (internal use only: PhysicsEngineBridge + bindings)
    // ==================================================================

    static PhysicsSystem& Instance();

    // ── Lifecycle ─────────────────────────────────────────────────────
    // Calling thread: MT only. Initialize -> Start -> Shutdown, strictly
    // ordered. Shutdown joins PT first (happens-before), then cleans up
    // VM and config.

    bool Initialize(const std::string& config_dir,
                    const std::string& assets_path,
                    const std::string& scripts_dir);
    bool Start();
    void Shutdown();

    bool IsRunning() const;
    bool IsInitialized() const { return is_initialized_; }

    // ── Command enqueue (MT -> PT, SPSC lock-free queue) ──────────────
    // The following 5 methods are called from the main thread. They enqueue
    // commands into the SPSC lock-free queue via PhysicsThread::EnqueueCommand.
    // No mutex is required.

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

    // ── Result retrieval (MT dequeues from PT, SPSC lock-free queue) ──
    std::optional<PhysicsFrameResult> FetchResult(uint64_t frame_id,
                                                   int timeout_ms);
    bool IsHealthy() const;

    // ── Save / Restore / Recovery ────────────────────────────────────
    std::string SaveState() const;
    bool RestoreState(const std::string& data);
    bool Recover(const std::string& saved_state = {});

    // ── Config hot-reload ─────────────────────────────────────────────
    bool ReloadThresholds();
    bool ReloadLogLevel();

    // ── Synchronous queries (MT -> direct PhysicsWorld access) ────────
    // Called from the main thread. These access PhysicsThread::GetWorld()
    // directly. Data access uses Jolt's BodyLockInterface for cross-thread
    // safety. PhysicsSystem needs no additional locking.

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

    // ── ScriptVM access ───────────────────────────────────────────────
    // GetScriptVM():          MT may call during Initialize (register bindings /
    //                         load scripts). After Start(), PT owns the Lua state.
    // GetPhysicsScriptVM():   PT-exclusive (used in UpdateScript()).

    ScriptVM& GetScriptVM() { return *script_vm_; }
    const ScriptVM& GetScriptVM() const { return *script_vm_; }

    PhysicsScriptVM& GetPhysicsScriptVM() { return *script_vm_; }
    const PhysicsScriptVM& GetPhysicsScriptVM() const { return *script_vm_; }

    // ── Custom pointer store — register subsystem objects in the VM ───
    //
    // Registers PhysicsSystem*, PhysicsThread*, PhysicsWorld*, and
    // PhysicsScriptVM* pointers into the VM's VMCustomPtrStore so they
    // can be retrieved from any lua_State* via typed accessors.
    //
    // Calling thread: MT (in Initialize(), before PT starts).

    void InitCustomPtrStore();

    // Retrieve subsystem objects from a physics-initialized lua_State.
    // Calling thread: PT (in Lua binding callbacks triggered by PT).

    static PhysicsSystem*    GetSystemFromState(lua_State* L);
    static PhysicsThread*    GetThreadFromState(lua_State* L);
    static PhysicsWorld*     GetWorldFromState(lua_State* L);
    static PhysicsScriptVM*  GetScriptVMFromState(lua_State* L);

    // ── Lua script update (PT only, via PostStepCallback after world_.Step()) ─
    //
    // This method runs exclusively on the physics thread (invoked by the
    // PostStepCallback registered in PhysicsSystem::Start()). It has
    // exclusive access to script_vm_'s Lua state:
    //   - Drives Lua coroutines (script_vm_->UpdateScript())
    //   - Calls on_physics_collision callbacks
    //
    // Because only the PT accesses script_vm_ after Start() (the MT never
    // touches it), no mutex protection is needed.
    //
    // Runtime guard: calls physics_thread_.VerifyIsPhysicsThread() at
    // entry — assert (debug) + log (all builds) if not on the PT.

    void UpdateScript(const std::vector<CollisionEvent>& collision_events);

    // ── Logger accessor ───────────────────────────────────────────────
    // Returns the physics thread's logger, or nullptr before Start() / after Stop().
    quill::Logger* GetPhysicsLogger() const { return physics_thread_.GetLogger(); }

    // ── Fixed delta time ──────────────────────────────────────────────
    float GetFixedDeltaTime() const {
        return config_manager_ ? config_manager_->GetPhysicsConfig().fixed_delta_time
                              : 0.01667f;
    }

private:
    // ==================================================================
    // Constructor private — accessible only via Instance() singleton
    // ==================================================================
    PhysicsSystem() = default;
    ~PhysicsSystem() = default;

    // ==================================================================
    // Friends — granted access to private members
    //
    // PhysicsEngineBridge: sole external API entry point.
    //   External modules can only reach PhysicsSystem indirectly through
    //   PhysicsEngineBridge. Therefore PhysicsEngineBridge needs access
    //   to all PhysicsSystem methods.
    //
    // ==================================================================
    friend class PhysicsEngineBridge;

    // ── Members ──────────────────────────────────────────────────────
    //
    // Thread ownership tags:
    //   [MT]   = main thread access (via PhysicsEngineBridge)
    //   [PT]   = physics thread access
    //   [MT->] = written by MT, happens-before PT reads
    //   [BOTH] = MT writes + PT reads (via SPSC queues or atomics)

    std::unique_ptr<PhysicsConfigManager> config_manager_;   // [MT] lifecycle
    std::unique_ptr<PhysicsScriptVM> script_vm_;              // [MT->] create/destroy on MT;
                                                               // [PT] Lua state PT-exclusive
    PhysicsThread physics_thread_;                             // [MT->] start/stop on MT;
                                                               // [PT] EventLoop runs on PT

    std::string config_dir_;     // [MT] config path
    std::string assets_path_;    // [MT->] asset path passed to PT
    std::string scripts_dir_;    // [MT] script directory

    std::atomic<bool> is_initialized_{false};                  // [MT] lifecycle methods only
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
