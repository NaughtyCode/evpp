#pragma once

//==============================================================================
// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// See physics_system.h for full documentation.
// Including this header without the macro will cause a compile-time #error.
//==============================================================================
#ifndef PHYSICS_INTERNAL_ACCESS
#error "physics_world.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <quill/Logger.h>

#include "runtime/physics/physics_config.h"
#include "runtime/physics/physics_commands.h"
#include "runtime/physics/physics_layers.h"
#include "runtime/physics/physics_materials.h"
#include "runtime/physics/physics_assets.h"
#include "runtime/physics/physics_diff.h"

namespace JPH {
class JobSystem;
class TempAllocator;
}

namespace engine {

//============================================================================
// ContactListenerImpl — thread-safe contact event collector [J8]
//
// Jolt callbacks run on its own job threads (JT). Events are appended to a
// mutex-protected buffer and collected by the physics thread after each
// Step() completes.
//============================================================================

class ContactListenerImpl final : public JPH::ContactListener {
public:
    struct ContactRecord {
        uint32_t body_a;
        uint32_t body_b;
        CollisionEvent::Type type;
        JPH::RVec3 contact_point_1;
        JPH::RVec3 contact_point_2;
    };

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& ioSettings) override;

    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& ioSettings) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1,
                                          const JPH::Body& inBody2,
                                          JPH::RVec3Arg inBaseOffset,
                                          const JPH::CollideShapeResult& inCollisionResult) override;

    // Called by PhysicsWorld after Step() to drain the event buffer.
    std::vector<ContactRecord> Drain();

private:
    void PushRecord(uint32_t body_a, uint32_t body_b,
                    CollisionEvent::Type type,
                    JPH::RVec3Arg cp1 = JPH::RVec3::sZero(),
                    JPH::RVec3Arg cp2 = JPH::RVec3::sZero());

    std::mutex mutex_;
    std::vector<ContactRecord> records_;
};

//============================================================================
// BodyActivationListenerImpl — tracks active body set for diff generation [J9]
//============================================================================

class BodyActivationListenerImpl final : public JPH::BodyActivationListener {
public:
    void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;
    void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;

    // Active body set (for diff filtering)
    bool IsActive(const JPH::BodyID& id) const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, bool> active_bodies_;
};

//============================================================================
// PhysicsWorld — wraps JPH::PhysicsSystem with full lifecycle management
//
// [Thread Model]
//
//   Wraps the complete lifecycle of the Jolt Physics engine. Its execution
//   environment involves multiple threads:
//
//     - Physics thread (PT):  Exclusive writer. EventLoop advances the
//       simulation via Step(), which internally uses Jolt's JobSystem for
//       parallel computation.
//     - Jolt Job threads (JT): Managed by JPH::JobSystem. Execute parallel
//       tasks (collision detection, constraint solving, etc.) during Step().
//       ContactListener and BodyActivationListener callbacks fire on JT
//       (see mutex notes below).
//     - Main thread (MT):    Read-only queries. GetTransform(), GetVelocity(),
//       RayCast(), etc. use Jolt's BodyLockInterface for cross-thread safety.
//
//   Internal locks:
//     - ContactListenerImpl::mutex_: JT writes -> PT reads via Drain()
//       (Drain() is only called after Step() completes and JT jobs have
//       finished, so there is no real contention; the mutex is defensive.)
//     - BodyActivationListenerImpl::mutex_: same pattern
//
//   Critical constraint:
//     - All write operations (CreateBody, DestroyBody, ApplyForce,
//       SetVelocity, Step) MUST execute on the PT.
//     - Main thread read operations are safe via Jolt BodyLockInterface.
//
// [External Access Constraint]
//
//   This class is a physics subsystem implementation detail. External modules
//   MUST NOT use PhysicsWorld directly. Compile-time protection is provided
//   by the PHYSICS_INTERNAL_ACCESS macro.
//============================================================================

class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // ── Initialization (strict 10-step order per [J2]) ──────────────────
    // Returns false on failure. Caller provides config + thresholds + logger
    // + asset path.
    bool Initialize(const PhysicsConfig& config,
                    const ThreadingConfig& threading,
                    const ThresholdsConfig& thresholds,
                    quill::Logger* logger,
                    const std::string& assets_path);

    // Hot-reload thresholds after initialization [D14]
    void SetThresholds(const ThresholdsConfig& thresholds);

    // ── Runtime body interface ──────────────────────────────────────────
    uint32_t CreateBody(const std::string& proto_id,
                        const JPH::RVec3& position,
                        const JPH::Quat& rotation,
                        uint64_t user_data = 0);
    bool DestroyBody(uint32_t body_id);
    bool ApplyForce(uint32_t body_id, const JPH::Vec3& force,
                    const JPH::RVec3& point);
    bool SetVelocity(uint32_t body_id, const JPH::Vec3& velocity);

    // ── Simulation ──────────────────────────────────────────────────────
    PhysicsFrameResult Step(float delta_time, uint64_t frame_id);

    // ── Save / Restore ──────────────────────────────────────────────────
    // Serialize full physics state to a binary blob. Thread-safe.
    std::string SaveState() const;
    // Restore physics state from a binary blob. Returns false on failure.
    bool RestoreState(const std::string& data);

    // ── Query ───────────────────────────────────────────────────────────
    std::optional<std::pair<JPH::RVec3, JPH::Quat>> GetTransform(uint32_t body_id) const;
    std::optional<JPH::Vec3> GetVelocity(uint32_t body_id) const;
    bool IsActive(uint32_t body_id) const;

    struct RayCastHit {
        uint32_t body_id = 0;
        double x = 0.0, y = 0.0, z = 0.0;
    };
    // Cast a ray into the physics world. Returns closest hit or nullopt.
    std::optional<RayCastHit> RayCast(const JPH::RVec3& origin,
                                       const JPH::Vec3& direction,
                                       float max_distance) const;

    struct Stats {
        uint32_t active_bodies = 0;
        uint32_t total_bodies = 0;
        int body_pairs = 0;
        int contact_constraints = 0;
    };
    Stats GetStats() const;

    // ── Object registry (exposed for asset loading) ────────────────────
    ObjectRegistry& GetRegistry() { return object_registry_; }
    const ObjectRegistry& GetRegistry() const { return object_registry_; }
    const std::unordered_map<std::string, PrototypeEntry>& GetPrototypes() const {
        return prototype_pool_;
    }

    // ── Accessors ───────────────────────────────────────────────────────
    JPH::PhysicsSystem& GetSystem() { return system_; }
    const JPH::PhysicsSystem& GetSystem() const { return system_; }
    quill::Logger* GetLogger() const { return logger_; }

private:
    // Step helpers
    void CollectTransforms(PhysicsFrameResult& result);
    void CollectCollisionEvents(PhysicsFrameResult& result);
    void GenerateDiffs(PhysicsFrameResult& result);

    // ── Members ─────────────────────────────────────────────────────────
    JPH::PhysicsSystem system_;
    std::unique_ptr<JPH::JobSystem> job_system_;
    std::unique_ptr<JPH::TempAllocator> temp_allocator_;

    std::unique_ptr<BPLayerInterfaceImpl> bp_layer_interface_;
    std::unique_ptr<ObjectLayerPairFilterImpl> layer_pair_filter_;
    std::unique_ptr<ObjectVSBLayerFilterImpl> obj_vs_bp_filter_;

    ContactListenerImpl contact_listener_;
    BodyActivationListenerImpl activation_listener_;

    quill::Logger* logger_ = nullptr;

    // Prototype pool: proto_id -> PrototypeEntry
    std::unordered_map<std::string, PrototypeEntry> prototype_pool_;

    // Object registry: body_id -> asset name
    ObjectRegistry object_registry_;

    // Per-body state snapshots (for diff generation)
    std::unordered_map<uint32_t, BodyStateSnapshot> state_snapshots_;

    // Config copy (for runtime access)
    PhysicsConfig config_;

    // Thresholds (set at init, hot-reloaded via SetThresholds)
    ThresholdsConfig thresholds_;

    // Per-frame stats tracking (updated in Step, returned by GetStats)
    std::atomic<int> last_body_pairs_{0};
    std::atomic<int> last_contact_constraints_{0};

    // Static guard for one-time Jolt init steps 1-3
    static std::atomic<bool> s_jolt_registered_;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
