#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_world.h"

#include "runtime/core/mem/mem.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>

#include "runtime/physics/physics_log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {

namespace {

bool IsFiniteVec3(JPH::Vec3Arg v) {
	return std::isfinite(v.GetX()) && std::isfinite(v.GetY()) && std::isfinite(v.GetZ());
}

bool IsFiniteRVec3(JPH::RVec3Arg v) {
	return std::isfinite(v.GetX()) && std::isfinite(v.GetY()) && std::isfinite(v.GetZ());
}

bool IsFiniteQuat(JPH::QuatArg q) {
	return std::isfinite(q.GetX()) && std::isfinite(q.GetY()) &&
		   std::isfinite(q.GetZ()) && std::isfinite(q.GetW());
}

JPH::Quat NormalizeOrIdentity(JPH::QuatArg q) {
	return q.LengthSq() > 1.0e-12f ? q.Normalized() : JPH::Quat::sIdentity();
}

void JoltTraceHandler(const char* fmt, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	ENGINE_LOG_WARN(GetLogger(), "Jolt: {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailedHandler(const char* expression,
							 const char* message,
							 const char* file,
							 JPH::uint line) {
	ENGINE_LOG_ERROR(GetLogger(),
					 "Jolt assert failed: expr=[{}], message=[{}], file=[{}:{}]",
					 expression ? expression : "",
					 message ? message : "",
					 file ? file : "",
					 line);
	return false;
}
#endif

}  // namespace

// Static guard for one-time Jolt registration (steps 1-3)

std::atomic<bool> PhysicsWorld::s_jolt_registered_{false};

// ContactListenerImpl

void ContactListenerImpl::PushRecord(uint32_t body_a,
									 uint32_t body_b,
									 CollisionEvent::Type type,
									 JPH::RVec3Arg cp1,
									 JPH::RVec3Arg cp2,
									 bool has_contact_points) {
	// Lock: serializes concurrent appends from multiple JT workers during
	// system_->Update(). Drain() is called after all JT workers finish, so
	// this lock never serializes JT against PT.
	std::lock_guard<std::mutex> lock(mutex_);
	records_.push_back({body_a, body_b, type, cp1, cp2, has_contact_points});
}

void ContactListenerImpl::OnContactAdded(const JPH::Body& inBody1,
										 const JPH::Body& inBody2,
										 const JPH::ContactManifold& inManifold,
										 JPH::ContactSettings& ioSettings) {
	JPH::RVec3 cp1 = inManifold.GetWorldSpaceContactPointOn1(0);
	JPH::RVec3 cp2 = inManifold.GetWorldSpaceContactPointOn2(0);
	PushRecord(inBody1.GetID().GetIndexAndSequenceNumber(),
			   inBody2.GetID().GetIndexAndSequenceNumber(),
			   CollisionEvent::Type::Start,
			   cp1,
			   cp2,
			   true);
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
	if (count > 0) {
		double inv = 1.0 / static_cast<double>(count);
		PushRecord(inBody1.GetID().GetIndexAndSequenceNumber(),
		   inBody2.GetID().GetIndexAndSequenceNumber(),
		   CollisionEvent::Type::Persist,
		   cp1_sum * static_cast<JPH::Real>(inv),
		   cp2_sum * static_cast<JPH::Real>(inv),
		   true);
	}
}

void ContactListenerImpl::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) {
	auto id1 = inSubShapePair.GetBody1ID();
	auto id2 = inSubShapePair.GetBody2ID();
	PushRecord(id1.GetIndexAndSequenceNumber(),
			   id2.GetIndexAndSequenceNumber(),
			   CollisionEvent::Type::End);
}

JPH::ValidateResult ContactListenerImpl::OnContactValidate(
	const JPH::Body& inBody1,
	const JPH::Body& inBody2,
	JPH::RVec3Arg inBaseOffset,
	const JPH::CollideShapeResult& inCollisionResult) {
	// Accept all contacts by default
	return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

std::vector<ContactListenerImpl::ContactRecord> ContactListenerImpl::Drain() {
	// Lock: technically unnecessary at this point (all JT workers have
	// finished - this is called from CollectCollisionEvents() after
	// system_->Update() returns). Included for consistency with PushRecord()
	// and as defense-in-depth against future code changes.
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<ContactRecord> drained;
	drained.swap(records_);
	return drained;
}

// BodyActivationListenerImpl

void BodyActivationListenerImpl::OnBodyActivated(const JPH::BodyID& inBodyID,
												 JPH::uint64 inBodyUserData) {
	// Lock: serializes JT writes (during system_->Update()) against
	// MT reads from GetStats(). Multiple JT workers may fire this
	// callback concurrently.
	std::lock_guard<std::mutex> lock(mutex_);
	active_bodies_[inBodyID.GetIndexAndSequenceNumber()] = true;
}

void BodyActivationListenerImpl::OnBodyDeactivated(const JPH::BodyID& inBodyID,
												   JPH::uint64 inBodyUserData) {
	// Lock: same reasoning as OnBodyActivated - JT writes vs MT reads.
	std::lock_guard<std::mutex> lock(mutex_);
	active_bodies_[inBodyID.GetIndexAndSequenceNumber()] = false;
}

bool BodyActivationListenerImpl::IsActive(const JPH::BodyID& id) const {
	// Lock: serializes against JT writes during system_->Update().
	// This is called from:
	//   - GenerateDiffs() on PT (after Update, no JT race, but lock
	//     is needed for consistency with the MT path below)
	//   - GetStats() on MT (may race with JT writes - lock is REQUIRED)
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = active_bodies_.find(id.GetIndexAndSequenceNumber());
	return it != active_bodies_.end() && it->second;
}

void BodyActivationListenerImpl::Clear() {
	// Lock: defense-in-depth. Clear() is only called from ~PhysicsWorld
	// after the physics thread has been joined (no concurrent access).
	std::lock_guard<std::mutex> lock(mutex_);
	active_bodies_.clear();
}

PhysicsWorld::PhysicsWorld() {
	system_.emplace();
}

// PhysicsWorld destructor

PhysicsWorld::~PhysicsWorld() {
	ResetRuntimeState(false);
}

void PhysicsWorld::ResetRuntimeState(bool recreate_system) {
	// Destroy in reverse order of creation
	system_.reset();
	activation_listener_.Clear();
	state_snapshots_.clear();
	prototype_pool_.clear();
	object_registry_.Clear();
	temp_allocator_.reset();
	job_system_.reset();
	obj_vs_bp_filter_.reset();
	layer_pair_filter_.reset();
	bp_layer_interface_.reset();
	last_body_pairs_.store(0, std::memory_order_release);
	last_contact_constraints_.store(0, std::memory_order_release);
	initialized_ = false;
	if (recreate_system) {
		system_.emplace();
	}

	// Note: Jolt Factory and RegisterTypes are program-global.
	// They are not cleaned up here for simplicity.
	// A full cleanup would need: JPH::UnregisterTypes() and delete Factory::sInstance.
}

// Initialize - strict 10-step order per [J2]

bool PhysicsWorld::Initialize(const PhysicsConfig& config,
							  const ThreadingConfig& threading,
							  const ThresholdsConfig& thresholds,
							  quill::Logger* logger,
							  const std::string& assets_path) {
	if (initialized_ || job_system_ || temp_allocator_ || bp_layer_interface_ || layer_pair_filter_ ||
		obj_vs_bp_filter_) {
		ResetRuntimeState();
	} else if (!system_) {
		system_.emplace();
	}
	logger_ = logger;
	config_ = config;
	{
		std::lock_guard<std::mutex> lock(thresholds_mutex_);
		thresholds_ = thresholds;
	}

	// Step 1-3: One-time Jolt registration (program-global)
	if (!s_jolt_registered_.exchange(true)) {
		JPH::Trace = JoltTraceHandler;
#ifdef JPH_ENABLE_ASSERTS
		JPH::AssertFailed = JoltAssertFailedHandler;
#endif
		JPH::RegisterDefaultAllocator();  // Step 1
		JPH::Factory::sInstance = CLOUDENGINE_MEM_NEW(JPH::Factory);  // Step 2
		JPH::RegisterTypes();  // Step 3
		PHYSICS_LOG_INFO(logger_, "JoltPhysics registered (allocator, factory, types)");
	}

	// Step 4: Create JobSystem
	if (threading.job_system_thread_count == 0 || threading.job_system_max_jobs <= 0) {
		// Single-threaded for debugging / deterministic verification
		job_system_ = std::make_unique<JPH::JobSystemSingleThreaded>(static_cast<unsigned int>(
			threading.job_system_max_jobs > 0 ? threading.job_system_max_jobs : 2048));
		PHYSICS_LOG_INFO(logger_, "PhysicsWorld: using single-threaded job system");
	} else {
		int thread_count = threading.job_system_thread_count;
		job_system_ = std::make_unique<JPH::JobSystemThreadPool>(
			static_cast<unsigned int>(threading.job_system_max_jobs),
			static_cast<unsigned int>(threading.job_system_max_barriers),
			thread_count);
		PHYSICS_LOG_INFO(logger_,
						 "PhysicsWorld: using thread pool job system, "
						 "max_jobs=[{}], max_barriers=[{}], threads=[{}]",
						 threading.job_system_max_jobs,
						 threading.job_system_max_barriers,
						 thread_count);
	}

	// Step 5: Create TempAllocator
	constexpr uint64_t kMinTempAllocatorBytes = 16ULL * 1024ULL * 1024ULL;
	uint64_t requested_temp_size =
		static_cast<uint64_t>(config.max_body_pairs) * 256ULL +
		static_cast<uint64_t>(config.max_contact_points) * 512ULL +
		1024ULL * 1024ULL;
	requested_temp_size = (std::max)(requested_temp_size, kMinTempAllocatorBytes);
	unsigned int temp_size = static_cast<unsigned int>(
		(std::min)(requested_temp_size,
				   static_cast<uint64_t>((std::numeric_limits<unsigned int>::max)())));
	unsigned int clamped = (temp_size > 0x7FFFFFFF) ? 0x7FFFFFFF : temp_size;
	temp_allocator_ = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(clamped);
	PHYSICS_LOG_INFO(logger_,
					 "PhysicsWorld: temp allocator with malloc fallback, size=[{} MB]",
					 clamped / (1024 * 1024));

	// Step 6: Construct layer interface instances
	// Use unique_ptr because base classes (BroadPhaseLayerInterface etc.)
	// inherit from NonCopyable and have no default constructors.
	const auto& lc = config.layer_config;
	bp_layer_interface_ = std::make_unique<BPLayerInterfaceImpl>(lc);
	layer_pair_filter_ = std::make_unique<ObjectLayerPairFilterImpl>(lc);
	obj_vs_bp_filter_ = std::make_unique<ObjectVSBLayerFilterImpl>(lc, *bp_layer_interface_);

	// Step 7: Init PhysicsSystem
	system_->Init(static_cast<unsigned int>(config.max_bodies),
				 static_cast<unsigned int>(config.num_body_mutexes),
				 static_cast<unsigned int>(config.max_body_pairs),
				 static_cast<unsigned int>(config.max_contact_points),
				 *bp_layer_interface_,
				 *obj_vs_bp_filter_,
				 *layer_pair_filter_);
	PHYSICS_LOG_INFO(logger_,
					 "PhysicsWorld: system initialized, "
					 "max_bodies=[{}], max_pairs=[{}], max_contacts=[{}]",
					 config.max_bodies,
					 config.max_body_pairs,
					 config.max_contact_points);

	// Step 8: Set physics settings + gravity
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

		system_->SetPhysicsSettings(settings);
		system_->SetGravity(JPH::Vec3(config.gravity_x, config.gravity_y, config.gravity_z));

		// [J13] Combine functions: Jolt defaults are geometric mean for
		// friction (sqrt(f1*f2)) and max for restitution (max(r1,r2)),
		// which match the design doc requirements. No explicit override
		// needed unless custom combine logic is desired.
	}

	// Step 9: Set listeners
	system_->SetContactListener(&contact_listener_);
	system_->SetBodyActivationListener(&activation_listener_);

	// Step 10: Load assets
	if (!assets_path.empty()) {
		AssetLoader loader;
		JPH::BodyInterface& bi = system_->GetBodyInterface();
		MaterialTable mt;  // empty; inline materials in asset JSON
		auto result = loader.LoadScene(assets_path, bi, *system_, mt, config.layer_config);
		if (!result.success) {
			PHYSICS_LOG_ERROR(
				logger_, "PhysicsWorld: failed to load assets [{}]: {}", assets_path, result.error);
			return false;
		}
		PHYSICS_LOG_INFO(logger_,
						 "PhysicsWorld: assets loaded -> "
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

	PHYSICS_LOG_INFO(logger_, "PhysicsWorld: initialization complete");
	initialized_ = true;
	return true;
}

// CreateBody - spawn dynamic body from prototype [D3]

uint32_t PhysicsWorld::CreateBody(const std::string& proto_id,
								  const JPH::RVec3& position,
								  const JPH::Quat& rotation,
								  uint64_t user_data) {
	if (proto_id.empty() || !IsFiniteRVec3(position) || !IsFiniteQuat(rotation)) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsWorld: invalid CreateBody request");
		return 0;
	}
	auto it = prototype_pool_.find(proto_id);
	if (it == prototype_pool_.end()) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsWorld: prototype '{}' not found", proto_id);
		return 0;
	}

	const auto& proto = it->second;

	JPH::BodyCreationSettings settings(
		proto.shape, position, NormalizeOrIdentity(rotation), proto.motion_type, proto.object_layer);
	if (proto.motion_type != JPH::EMotionType::Static && proto.mass > 0.0f) {
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	}
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

	JPH::BodyInterface& bi = system_->GetBodyInterface();
	JPH::Body* body = bi.CreateBody(settings);
	if (!body) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsWorld: failed to create body from '{}'", proto_id);
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

// DestroyBody [D3]

bool PhysicsWorld::DestroyBody(uint32_t body_id) {
	JPH::BodyID jid(body_id);
	JPH::BodyInterface& bi = system_->GetBodyInterface();

	if (!bi.IsAdded(jid)) {
		return false;
	}

	bi.RemoveBody(jid);
	bi.DestroyBody(jid);

	object_registry_.Unregister(body_id);
	state_snapshots_.erase(body_id);

	return true;
}

// ApplyForce [D3]

bool PhysicsWorld::ApplyForce(uint32_t body_id, const JPH::Vec3& force, const JPH::RVec3& point) {
	if (!IsFiniteVec3(force) || !IsFiniteRVec3(point)) {
		return false;
	}
	JPH::BodyID jid(body_id);
	JPH::BodyInterface& bi = system_->GetBodyInterface();

	if (!bi.IsAdded(jid)) return false;

	bi.AddForce(jid, force, point);
	if (!bi.IsActive(jid)) {
		bi.ActivateBody(jid);
	}
	return true;
}

// SetVelocity [D3]

bool PhysicsWorld::SetVelocity(uint32_t body_id, const JPH::Vec3& velocity) {
	if (!IsFiniteVec3(velocity)) {
		return false;
	}
	JPH::BodyID jid(body_id);
	JPH::BodyInterface& bi = system_->GetBodyInterface();

	if (!bi.IsAdded(jid)) return false;

	bi.SetLinearVelocity(jid, velocity);
	if (!bi.IsActive(jid)) {
		bi.ActivateBody(jid);
	}
	return true;
}

// Step - execute one physics simulation step [D5][D6]

PhysicsFrameResult PhysicsWorld::Step(float delta_time, uint64_t frame_id) {
	PhysicsFrameResult result;
	result.frame_id = frame_id;

	if (!std::isfinite(delta_time) || delta_time <= 0.0f) {
		result.error = "delta_time must be finite and > 0";
		return result;
	}

	// Run Jolt Update + post-processing
	{
		ENGINE_PROFILE_PHYSICS_STEP(delta_time);

		JPH::EPhysicsUpdateError err = system_->Update(
			delta_time, config_.sub_step_count, temp_allocator_.get(), job_system_.get());

		if (err != JPH::EPhysicsUpdateError::None) {
			// Map error to string
			std::string err_str;
			if (static_cast<unsigned int>(err) &
				static_cast<unsigned int>(JPH::EPhysicsUpdateError::ManifoldCacheFull))
				err_str = "ManifoldCacheFull";
			if (static_cast<unsigned int>(err) &
				static_cast<unsigned int>(JPH::EPhysicsUpdateError::BodyPairCacheFull))
				err_str += (err_str.empty() ? "" : ", ") + std::string("BodyPairCacheFull");
			if (static_cast<unsigned int>(err) &
				static_cast<unsigned int>(JPH::EPhysicsUpdateError::ContactConstraintsFull))
				err_str += (err_str.empty() ? "" : ", ") + std::string("ContactConstraintsFull");

			PHYSICS_LOG_ERROR(
				logger_, "PhysicsWorld: Update error - frame=[{}], error=[{}]", frame_id, err_str);
			result.error = err_str;
			return result;
		}

		// Post-step: collect transforms, collision events, and diffs
		{
			ENGINE_PROFILE_PHYSICS_TRANSFORM();
			CollectTransforms(result);
		}  // Transform slice ends

		{
			ENGINE_PROFILE_PHYSICS_COLLISION();
			CollectCollisionEvents(result);
		}  // Collision slice ends

		{
			ENGINE_PROFILE_PHYSICS_DIFF();
			GenerateDiffs(result);
		}  // Diff slice ends

	}  // PhysicsStep slice ends

	// Track per-frame stats for GetStats()
	{
		last_body_pairs_ = static_cast<int>(result.collision_events.size());
		int total_contacts = 0;
		for (const auto& evt : result.collision_events) {
			total_contacts += static_cast<int>(evt.contact_points.size());
		}
		last_contact_constraints_ = total_contacts;
	}

	return result;
}

// CollectTransforms - snapshot all active dynamic bodies

void PhysicsWorld::CollectTransforms(PhysicsFrameResult& result) {
	JPH::BodyInterface& bi = system_->GetBodyInterfaceNoLock();

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

// CollectCollisionEvents - drain ContactListener buffer

void PhysicsWorld::CollectCollisionEvents(PhysicsFrameResult& result) {
	auto records = contact_listener_.Drain();

	// Group records by body pair to deduplicate contact points
	std::unordered_map<uint64_t, CollisionEvent> event_map;

	for (const auto& rec : records) {
		uint32_t lo = (rec.body_a < rec.body_b) ? rec.body_a : rec.body_b;
		uint32_t hi = (rec.body_a < rec.body_b) ? rec.body_b : rec.body_a;
		uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;

		auto& evt = event_map[key];
		if (evt.body_a == 0 && evt.body_b == 0) {
			evt.body_a = rec.body_a;
			evt.body_b = rec.body_b;
			evt.type = rec.type;
		} else if (rec.type != evt.type) {
			// Update type if a later event is more significant (e.g. Persist after Start)
			evt.type = rec.type;
		}
		// Collect both contact points. A real contact can be exactly at the
		// world origin, so validity must not be inferred from non-zero values.
		if (rec.has_contact_points) {
			evt.contact_points.push_back(rec.contact_point_1);
			evt.contact_points.push_back(rec.contact_point_2);
		}
	}

	for (auto& [_, evt] : event_map) {
		result.collision_events.push_back(std::move(evt));
	}
}

// GenerateDiffs - produce DiffPackets for changed bodies

void PhysicsWorld::GenerateDiffs(PhysicsFrameResult& result) {
	JPH::BodyInterface& bi = system_->GetBodyInterfaceNoLock();
	ThresholdsConfig thresholds;
	{
		std::lock_guard<std::mutex> lock(thresholds_mutex_);
		thresholds = thresholds_;
	}

	for (auto& [body_id, previous] : state_snapshots_) {
		JPH::BodyID jid(body_id);
		if (!bi.IsAdded(jid)) {
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

// Query helpers

std::optional<std::pair<JPH::RVec3, JPH::Quat>> PhysicsWorld::GetTransform(uint32_t body_id) const {
	JPH::BodyID jid(body_id);
	JPH::BodyLockRead lock(system_->GetBodyLockInterface(), jid);
	if (!lock.Succeeded()) return std::nullopt;
	const JPH::Body& body = lock.GetBody();
	return std::make_pair(body.GetPosition(), body.GetRotation());
}

std::optional<JPH::Vec3> PhysicsWorld::GetVelocity(uint32_t body_id) const {
	JPH::BodyID jid(body_id);
	JPH::BodyLockRead lock(system_->GetBodyLockInterface(), jid);
	if (!lock.Succeeded()) return std::nullopt;
	const JPH::Body& body = lock.GetBody();
	return body.GetLinearVelocity();
}

bool PhysicsWorld::IsActive(uint32_t body_id) const {
	JPH::BodyID jid(body_id);
	JPH::BodyLockRead lock(system_->GetBodyLockInterface(), jid);
	if (!lock.Succeeded()) return false;
	return lock.GetBody().IsActive();
}

PhysicsWorld::Stats PhysicsWorld::GetStats() const {
	Stats s;
	auto body_stats = system_->GetBodyStats();
	s.total_bodies = static_cast<uint32_t>(body_stats.mNumBodies);
	s.active_bodies = static_cast<uint32_t>(body_stats.mNumActiveBodiesDynamic +
											body_stats.mNumActiveBodiesKinematic +
											body_stats.mNumActiveSoftBodies);
	s.body_pairs = last_body_pairs_;
	s.contact_constraints = last_contact_constraints_;
	return s;
}

void PhysicsWorld::SetThresholds(const ThresholdsConfig& thresholds) {
	std::lock_guard<std::mutex> lock(thresholds_mutex_);
	thresholds_ = thresholds;
}

std::optional<PhysicsWorld::RayCastHit> PhysicsWorld::RayCast(const JPH::RVec3& origin,
															  const JPH::Vec3& direction,
															  float max_distance) const {
	const float direction_len_sq = direction.LengthSq();
	if (!IsFiniteRVec3(origin) || !std::isfinite(max_distance) || max_distance <= 0.0f ||
		!std::isfinite(direction_len_sq) || direction_len_sq <= 1.0e-12f) {
		return std::nullopt;
	}

	JPH::Vec3 ray_delta = direction.Normalized() * max_distance;
	JPH::RRayCast ray(origin, ray_delta);
	JPH::RayCastResult hit;
	if (!system_->GetNarrowPhaseQuery().CastRay(ray, hit)) {
		return std::nullopt;
	}
	if (hit.mFraction < 0.0f || hit.mFraction > 1.0f) {
		return std::nullopt;
	}
	JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
	return RayCastHit{static_cast<uint32_t>(hit.mBodyID.GetIndexAndSequenceNumber()),
					  point.GetX(),
					  point.GetY(),
					  point.GetZ()};
}

std::string PhysicsWorld::SaveState() const {
	JPH::StateRecorderImpl recorder;
	system_->SaveState(recorder);
	return recorder.GetData();
}

bool PhysicsWorld::RestoreState(const std::string& data) {
	// StateRecorderImpl writes to an internal stringstream.
	// To restore: write saved data into the recorder, rewind to
	// switch it to read mode, then restore into the physics system.
	JPH::StateRecorderImpl recorder;
	recorder.WriteBytes(data.data(), data.size());
	recorder.Rewind();
	bool ok = system_->RestoreState(recorder);
	if (ok) {
		RebuildStateSnapshots();
	}
	return ok;
}

void PhysicsWorld::RebuildStateSnapshots() {
	state_snapshots_.clear();

	JPH::BodyIDVector body_ids;
	system_->GetBodies(body_ids);
	for (const JPH::BodyID& id : body_ids) {
		JPH::BodyLockRead lock(system_->GetBodyLockInterface(), id);
		if (!lock.Succeeded()) {
			continue;
		}
		const JPH::Body& body = lock.GetBody();
		if (body.GetMotionType() == JPH::EMotionType::Static) {
			continue;
		}

		BodyStateSnapshot snapshot;
		snapshot.position = body.GetPosition();
		snapshot.rotation = body.GetRotation();
		snapshot.linear_velocity = body.GetLinearVelocity();
		snapshot.angular_velocity = body.GetAngularVelocity();

		uint32_t body_id = id.GetIndexAndSequenceNumber();
		state_snapshots_[body_id] = snapshot;
		if (!object_registry_.Has(body_id)) {
			object_registry_.Register(body_id, "");
		}
	}
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
