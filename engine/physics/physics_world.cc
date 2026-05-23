#ifdef ENGINE_PHYSICS_ENABLED

#include "engine/physics/physics_world.h"

#include <cmath>
#include <cstdio>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

#include "engine/core/log/log_macros.h"

namespace engine {

//============================================================================
// Static guard for one-time Jolt registration (steps 1-3)
//============================================================================

std::atomic<bool> PhysicsWorld::s_jolt_registered_{false};

//============================================================================
// ContactListenerImpl
//============================================================================

void ContactListenerImpl::PushRecord(uint32_t body_a, uint32_t body_b,
                                      CollisionEvent::Type type,
                                      JPH::RVec3Arg cp1, JPH::RVec3Arg cp2) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back({body_a, body_b, type, cp1, cp2});
}

void ContactListenerImpl::OnContactAdded(const JPH::Body& inBody1,
                                          const JPH::Body& inBody2,
                                          const JPH::ContactManifold& inManifold,
                                          JPH::ContactSettings& ioSettings) {
    JPH::RVec3 cp1 = inManifold.GetWorldSpaceContactPointOn1(0);
    JPH::RVec3 cp2 = inManifold.GetWorldSpaceContactPointOn2(0);
    PushRecord(inBody1.GetID().GetIndexAndSequenceNumber(),
               inBody2.GetID().GetIndexAndSequenceNumber(),
               CollisionEvent::Type::Start, cp1, cp2);
}

void ContactListenerImpl::OnContactPersisted(const JPH::Body& inBody1,
                                              const JPH::Body& inBody2,
                                              const JPH::ContactManifold& inManifold,
                                              JPH::ContactSettings& ioSettings) {
    // Collect all contact points for persisted contacts
    JPH::RVec3 cp1_sum = JPH::RVec3::sZero();
    JPH::RVec3 cp2_sum = JPH::RVec3::sZero();
    int count = std::min(static_cast<int>(inManifold.mRelativeContactPointsOn1.size()), 4);
    for (int i = 0; i < count; ++i) {
        cp1_sum += inManifold.GetWorldSpaceContactPointOn1(i);
        cp2_sum += inManifold.GetWorldSpaceContactPointOn2(i);
    }
    double inv = 1.0 / static_cast<double>(count);
    PushRecord(inBody1.GetID().GetIndexAndSequenceNumber(),
               inBody2.GetID().GetIndexAndSequenceNumber(),
               CollisionEvent::Type::Persist, cp1_sum * inv, cp2_sum * inv);
}

void ContactListenerImpl::OnContactRemoved(
    const JPH::SubShapeIDPair& inSubShapePair) {
    auto id1 = inSubShapePair.GetBody1ID();
    auto id2 = inSubShapePair.GetBody2ID();
    PushRecord(id1.GetIndexAndSequenceNumber(),
               id2.GetIndexAndSequenceNumber(),
               CollisionEvent::Type::End);
}

JPH::ValidateResult ContactListenerImpl::OnContactValidate(
    const JPH::Body& inBody1, const JPH::Body& inBody2,
    JPH::RVec3Arg inBaseOffset,
    const JPH::CollideShapeResult& inCollisionResult) {
    // Accept all contacts by default
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

std::vector<ContactListenerImpl::ContactRecord> ContactListenerImpl::Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ContactRecord> drained;
    drained.swap(records_);
    return drained;
}

//============================================================================
// BodyActivationListenerImpl
//============================================================================

void BodyActivationListenerImpl::OnBodyActivated(const JPH::BodyID& inBodyID,
                                                   JPH::uint64 inBodyUserData) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_bodies_[inBodyID.GetIndexAndSequenceNumber()] = true;
}

void BodyActivationListenerImpl::OnBodyDeactivated(const JPH::BodyID& inBodyID,
                                                     JPH::uint64 inBodyUserData) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_bodies_[inBodyID.GetIndexAndSequenceNumber()] = false;
}

bool BodyActivationListenerImpl::IsActive(const JPH::BodyID& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_bodies_.find(id.GetIndexAndSequenceNumber());
    return it != active_bodies_.end() && it->second;
}

void BodyActivationListenerImpl::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_bodies_.clear();
}

//============================================================================
// PhysicsWorld destructor
//============================================================================

PhysicsWorld::~PhysicsWorld() {
    // Destroy in reverse order of creation
    activation_listener_.Clear();
    state_snapshots_.clear();
    prototype_pool_.clear();
    object_registry_.Clear();
    temp_allocator_.reset();
    job_system_.reset();

    // Note: Jolt Factory and RegisterTypes are program-global.
    // They are not cleaned up here for simplicity.
    // A full cleanup would need: JPH::UnregisterTypes() and delete Factory::sInstance.
}

//============================================================================
// Initialize — strict 10-step order per [J2]
//============================================================================

bool PhysicsWorld::Initialize(const PhysicsConfig& config,
                               const ThreadingConfig& threading,
                               quill::Logger* logger,
                               const std::string& assets_path) {
    logger_ = logger;
    config_ = config;
    thresholds_ = nullptr;  // loaded separately by PhysicsConfigManager

    // ── Step 1-3: One-time Jolt registration (program-global) ──────────
    if (!s_jolt_registered_.exchange(true)) {
        JPH::RegisterDefaultAllocator();                                // Step 1
        JPH::Factory::sInstance = new JPH::Factory();                   // Step 2
        JPH::RegisterTypes();                                           // Step 3
        ENGINE_LOG_INFO(logger_, "JoltPhysics registered (allocator, factory, types)");
    }

    // ── Step 4: Create JobSystem ────────────────────────────────────────
    if (threading.job_system_thread_count == 0 || threading.job_system_max_jobs <= 0) {
        // Single-threaded for debugging / deterministic verification
        job_system_ = std::make_unique<JPH::JobSystemSingleThreaded>(
            static_cast<unsigned int>(threading.job_system_max_jobs > 0 ? threading.job_system_max_jobs : 2048));
        ENGINE_LOG_INFO(logger_, "PhysicsWorld: using single-threaded job system");
    } else {
        int thread_count = threading.job_system_thread_count;
        job_system_ = std::make_unique<JPH::JobSystemThreadPool>(
            static_cast<unsigned int>(threading.job_system_max_jobs),
            static_cast<unsigned int>(threading.job_system_max_barriers),
            thread_count);
        ENGINE_LOG_INFO(logger_, "PhysicsWorld: using thread pool job system, "
                        "max_jobs=[{}], max_barriers=[{}], threads=[{}]",
                        threading.job_system_max_jobs,
                        threading.job_system_max_barriers,
                        thread_count);
    }

    // ── Step 5: Create TempAllocator ──────────────────────────────────
    unsigned int temp_size = config.max_body_pairs * 256;
    if (temp_size > 256 * 1024 * 1024) {
        // Use malloc fallback for very large configs
        unsigned int clamped = (temp_size > 0x7FFFFFFF) ? 0x7FFFFFFF : temp_size;
        temp_allocator_ = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(clamped);
        ENGINE_LOG_INFO(logger_, "PhysicsWorld: temp allocator with malloc fallback, "
                        "size=[{} MB]", clamped / (1024 * 1024));
    } else {
        temp_allocator_ = std::make_unique<JPH::TempAllocatorImpl>(temp_size);
        ENGINE_LOG_INFO(logger_, "PhysicsWorld: temp allocator, size=[{} KB]",
                        temp_size / 1024);
    }

    // ── Step 6: Construct layer interface instances ──────────────────
    // Use unique_ptr because base classes (BroadPhaseLayerInterface etc.)
    // inherit from NonCopyable and have no default constructors.
    const auto& lc = config.layer_config;
    bp_layer_interface_ = std::make_unique<BPLayerInterfaceImpl>(lc);
    layer_pair_filter_ = std::make_unique<ObjectLayerPairFilterImpl>(lc);
    obj_vs_bp_filter_ = std::make_unique<ObjectVSBLayerFilterImpl>(lc, *bp_layer_interface_);

    // ── Step 7: Init PhysicsSystem ────────────────────────────────────
    system_.Init(
        static_cast<unsigned int>(config.max_bodies),
        static_cast<unsigned int>(config.num_body_mutexes),
        static_cast<unsigned int>(config.max_body_pairs),
        static_cast<unsigned int>(config.max_contact_points),
        *bp_layer_interface_,
        *obj_vs_bp_filter_,
        *layer_pair_filter_
    );
    ENGINE_LOG_INFO(logger_, "PhysicsWorld: system initialized, "
                    "max_bodies=[{}], max_pairs=[{}], max_contacts=[{}]",
                    config.max_bodies, config.max_body_pairs,
                    config.max_contact_points);

    // ── Step 8: Set physics settings + gravity ───────────────────────
    {
        JPH::PhysicsSettings settings;
        settings.mNumVelocitySteps = config.solver_iterations;
        settings.mNumPositionSteps = config.position_iterations;
        settings.mSpeculativeContactDistance = config.speculative_contact_distance;
        settings.mPenetrationSlop = config.penetration_slop;
        settings.mBaumgarte = config.baumgarte;
        settings.mTimeBeforeSleep = config.time_before_sleep;
        settings.mPointVelocitySleepThreshold = config.point_velocity_sleep_threshold;
        settings.mDeterministicSimulation = config.deterministic_simulation;
        settings.mConstraintWarmStart = config.constraint_warm_start;
        settings.mAllowSleeping = config.allow_sleeping;
        settings.mUseLargeIslandSplitter = config.use_large_island_splitter;
        settings.mLinearCastThreshold = config.linear_cast_threshold;
        settings.mLinearCastMaxPenetration = config.linear_cast_max_penetration;
        settings.mMaxPenetrationDistance = config.max_penetration_distance;
        settings.mMinVelocityForRestitution = config.min_velocity_for_restitution;
        settings.mUseBodyPairContactCache = config.use_body_pair_contact_cache;
        settings.mUseManifoldReduction = config.use_manifold_reduction;
        settings.mCheckActiveEdges = config.check_active_edges;

        system_.SetPhysicsSettings(settings);
        system_.SetGravity(JPH::Vec3(config.gravity_x, config.gravity_y, config.gravity_z));
    }

    // ── Step 9: Set listeners ───────────────────────────────────────
    system_.SetContactListener(&contact_listener_);
    system_.SetBodyActivationListener(&activation_listener_);

    // ── Step 10: Load assets ─────────────────────────────────────────
    if (!assets_path.empty()) {
        AssetLoader loader;
        JPH::BodyInterface& bi = system_.GetBodyInterface();
        MaterialTable mt;  // empty; inline materials in asset JSON
        auto result = loader.LoadScene(assets_path, bi, system_, mt, config.layer_config);
        if (!result.success) {
            ENGINE_LOG_ERROR(logger_, "PhysicsWorld: failed to load assets [{}]: {}",
                             assets_path, result.error);
            return false;
        }
        ENGINE_LOG_INFO(logger_, "PhysicsWorld: assets loaded — "
                        "[{}] static bodies, [{}] prototypes, [{}] constraints",
                        result.static_bodies_loaded,
                        result.dynamic_prototypes_loaded,
                        result.constraints_loaded);

        // Copy loaded data into PhysicsWorld
        for (const auto& [proto_id, entry] : loader.GetPrototypes()) {
            prototype_pool_[proto_id] = entry;
        }
        for (const auto& [body_id, asset_name] : loader.GetStaticBodyIds()) {
            object_registry_.Register(body_id, asset_name);
        }
    }

    ENGINE_LOG_INFO(logger_, "PhysicsWorld: initialization complete");
    return true;
}

//============================================================================
// CreateBody — spawn dynamic body from prototype [D3]
//============================================================================

uint32_t PhysicsWorld::CreateBody(const std::string& proto_id,
                                   const JPH::RVec3& position,
                                   const JPH::Quat& rotation,
                                   uint64_t user_data) {
    auto it = prototype_pool_.find(proto_id);
    if (it == prototype_pool_.end()) {
        ENGINE_LOG_ERROR(logger_, "PhysicsWorld: prototype '{}' not found", proto_id);
        return 0;
    }

    const auto& proto = it->second;

    JPH::BodyCreationSettings settings(
        proto.shape, position, rotation,
        proto.motion_type, proto.object_layer);
    settings.mMassPropertiesOverride.mMass = proto.mass;
    settings.mFriction = proto.friction;
    settings.mRestitution = proto.restitution;
    settings.mLinearDamping = proto.linear_damping;
    settings.mAngularDamping = proto.angular_damping;
    settings.mGravityFactor = proto.gravity_factor;
    settings.mMotionQuality = proto.motion_quality;
    settings.mIsSensor = proto.is_sensor;
    settings.mAllowSleeping = proto.allow_sleeping;
    settings.mMaxLinearVelocity = proto.max_linear_velocity;
    settings.mMaxAngularVelocity = proto.max_angular_velocity;

    // Set allowed DOFs
    settings.mAllowedDOFs = JPH::EAllowedDOFs(proto.allowed_dofs);
    settings.mUserData = user_data;

    JPH::BodyInterface& bi = system_.GetBodyInterface();
    JPH::Body* body = bi.CreateBody(settings);
    if (!body) {
        ENGINE_LOG_ERROR(logger_, "PhysicsWorld: failed to create body from '{}'",
                         proto_id);
        return 0;
    }

    uint32_t body_id = body->GetID().GetIndexAndSequenceNumber();
    bi.AddBody(body->GetID(), JPH::EActivation::Activate);

    // Register in object registry and initialize state snapshot
    object_registry_.Register(body_id, proto_id);
    BodyStateSnapshot snap;
    snap.position = position;
    snap.rotation = rotation;
    snap.linear_velocity = JPH::Vec3::sZero();
    snap.angular_velocity = JPH::Vec3::sZero();
    state_snapshots_[body_id] = snap;

    return body_id;
}

//============================================================================
// DestroyBody [D3]
//============================================================================

bool PhysicsWorld::DestroyBody(uint32_t body_id) {
    JPH::BodyID jid(body_id);
    JPH::BodyInterface& bi = system_.GetBodyInterface();

    if (!bi.IsAdded(jid)) {
        return false;
    }

    bi.RemoveBody(jid);
    bi.DestroyBody(jid);

    object_registry_.Unregister(body_id);
    state_snapshots_.erase(body_id);

    return true;
}

//============================================================================
// ApplyForce [D3]
//============================================================================

bool PhysicsWorld::ApplyForce(uint32_t body_id,
                               const JPH::Vec3& force,
                               const JPH::RVec3& point) {
    JPH::BodyID jid(body_id);
    JPH::BodyInterface& bi = system_.GetBodyInterface();

    if (!bi.IsAdded(jid)) return false;

    bi.AddForce(jid, force, point);
    if (!bi.IsActive(jid)) {
        bi.ActivateBody(jid);
    }
    return true;
}

//============================================================================
// SetVelocity [D3]
//============================================================================

bool PhysicsWorld::SetVelocity(uint32_t body_id, const JPH::Vec3& velocity) {
    JPH::BodyID jid(body_id);
    JPH::BodyInterface& bi = system_.GetBodyInterface();

    if (!bi.IsAdded(jid)) return false;

    bi.SetLinearVelocity(jid, velocity);
    if (!bi.IsActive(jid)) {
        bi.ActivateBody(jid);
    }
    return true;
}

//============================================================================
// Step — execute one physics simulation step [D5][D6]
//============================================================================

PhysicsFrameResult PhysicsWorld::Step(float delta_time, uint64_t frame_id) {
    PhysicsFrameResult result;
    result.frame_id = frame_id;

    if (delta_time <= 0.0f) {
        result.error = "delta_time <= 0";
        return result;
    }

    // Run Jolt Update
    JPH::EPhysicsUpdateError err = system_.Update(
        delta_time,
        config_.sub_step_count,
        temp_allocator_.get(),
        job_system_.get());

    if (err != JPH::EPhysicsUpdateError::None) {
        // Map error to string
        std::string err_str;
        if (static_cast<unsigned int>(err) & static_cast<unsigned int>(JPH::EPhysicsUpdateError::ManifoldCacheFull))
            err_str = "ManifoldCacheFull";
        if (static_cast<unsigned int>(err) & static_cast<unsigned int>(JPH::EPhysicsUpdateError::BodyPairCacheFull))
            err_str += (err_str.empty() ? "" : ", ") + std::string("BodyPairCacheFull");
        if (static_cast<unsigned int>(err) & static_cast<unsigned int>(JPH::EPhysicsUpdateError::ContactConstraintsFull))
            err_str += (err_str.empty() ? "" : ", ") + std::string("ContactConstraintsFull");

        ENGINE_LOG_ERROR(logger_, "PhysicsWorld: Update error — frame=[{}], error=[{}]",
                         frame_id, err_str);
        result.error = err_str;
        return result;
    }

    // Post-step: collect transforms, collision events, and diffs
    CollectTransforms(result);
    CollectCollisionEvents(result);
    GenerateDiffs(result);

    return result;
}

//============================================================================
// CollectTransforms — snapshot all active dynamic bodies
//============================================================================

void PhysicsWorld::CollectTransforms(PhysicsFrameResult& result) {
    JPH::BodyInterface& bi = system_.GetBodyInterfaceNoLock();

    for (auto& [body_id, snap] : state_snapshots_) {
        JPH::BodyID jid(body_id);
        if (!bi.IsAdded(jid) || !bi.IsActive(jid)) {
            continue;
        }

        JPH::RVec3 pos = bi.GetPosition(jid);
        JPH::Quat rot = bi.GetRotation(jid);

        BodyTransform bt;
        bt.body_id = body_id;
        bt.pos_x = pos.GetX();
        bt.pos_y = pos.GetY();
        bt.pos_z = pos.GetZ();
        bt.rot_x = rot.GetX();
        bt.rot_y = rot.GetY();
        bt.rot_z = rot.GetZ();
        bt.rot_w = rot.GetW();
        result.transforms.push_back(bt);
    }
}

//============================================================================
// CollectCollisionEvents — drain ContactListener buffer
//============================================================================

void PhysicsWorld::CollectCollisionEvents(PhysicsFrameResult& result) {
    auto records = contact_listener_.Drain();

    // Group records by body pair to deduplicate contact points
    std::unordered_map<uint64_t, CollisionEvent> event_map;

    for (const auto& rec : records) {
        uint64_t key = (static_cast<uint64_t>(rec.body_a) << 32) | rec.body_b;

        auto& evt = event_map[key];
        if (evt.body_a == 0 && evt.body_b == 0) {
            evt.body_a = rec.body_a;
            evt.body_b = rec.body_b;
            evt.type = rec.type;
        }
        // Collect both contact points
        if (rec.contact_point_1 != JPH::RVec3::sZero()) {
            evt.contact_points.push_back(rec.contact_point_1);
        }
        if (rec.contact_point_2 != JPH::RVec3::sZero()) {
            evt.contact_points.push_back(rec.contact_point_2);
        }
    }

    for (auto& [_, evt] : event_map) {
        result.collision_events.push_back(std::move(evt));
    }
}

//============================================================================
// GenerateDiffs — produce DiffPackets for changed bodies
//============================================================================

void PhysicsWorld::GenerateDiffs(PhysicsFrameResult& result) {
    JPH::BodyInterface& bi = system_.GetBodyInterfaceNoLock();
    const ThresholdsConfig& thresholds = *thresholds_;

    for (auto& [body_id, previous] : state_snapshots_) {
        JPH::BodyID jid(body_id);
        if (!bi.IsAdded(jid)) {
            continue;
        }

        // Only generate diffs for active dynamic bodies
        if (!activation_listener_.IsActive(jid)) {
            continue;
        }

        BodyStateSnapshot current;
        current.position = bi.GetPosition(jid);
        current.rotation = bi.GetRotation(jid);
        current.linear_velocity = bi.GetLinearVelocity(jid);
        current.angular_velocity = bi.GetAngularVelocity(jid);

        auto diff = GenerateDiff(body_id, current, previous, thresholds);
        if (diff.has_value()) {
            result.diff_packets.push_back(std::move(*diff));
        }

        // Update snapshot for next frame
        previous = current;
    }
}

//============================================================================
// Query helpers
//============================================================================

std::optional<std::pair<JPH::RVec3, JPH::Quat>> PhysicsWorld::GetTransform(
    uint32_t body_id) const {
    JPH::BodyID jid(body_id);
    JPH::BodyLockRead lock(system_.GetBodyLockInterface(), jid);
    if (!lock.Succeeded()) return std::nullopt;
    const JPH::Body& body = lock.GetBody();
    return std::make_pair(body.GetPosition(), body.GetRotation());
}

std::optional<JPH::Vec3> PhysicsWorld::GetVelocity(uint32_t body_id) const {
    JPH::BodyID jid(body_id);
    JPH::BodyLockRead lock(system_.GetBodyLockInterface(), jid);
    if (!lock.Succeeded()) return std::nullopt;
    const JPH::Body& body = lock.GetBody();
    return body.GetLinearVelocity();
}

bool PhysicsWorld::IsActive(uint32_t body_id) const {
    JPH::BodyID jid(body_id);
    return system_.GetBodyInterfaceNoLock().IsActive(jid);
}

PhysicsWorld::Stats PhysicsWorld::GetStats() const {
    Stats s;
    s.total_bodies = static_cast<uint32_t>(state_snapshots_.size());
    for (const auto& [id, _] : state_snapshots_) {
        if (activation_listener_.IsActive(JPH::BodyID(id))) {
            ++s.active_bodies;
        }
    }
    return s;
}

std::optional<PhysicsWorld::RayCastHit> PhysicsWorld::RayCast(
    const JPH::RVec3& origin, const JPH::Vec3& direction,
    float max_distance) const {
    JPH::RRayCast ray(origin, direction);
    JPH::RayCastResult hit;
    if (!system_.GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return std::nullopt;
    }
    if (hit.mFraction > max_distance) {
        return std::nullopt;
    }
    JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    return RayCastHit{
        static_cast<uint32_t>(hit.mBodyID.GetIndexAndSequenceNumber()),
        point.GetX(), point.GetY(), point.GetZ()
    };
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
