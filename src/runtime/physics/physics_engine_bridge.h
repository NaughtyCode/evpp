#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include "runtime/core/engine_api.h"

// PHYSICS_API: only use dllexport/dllimport when physics is actually enabled.
// When disabled, the class consists entirely of inline stubs that must be
// compiled directly into each translation unit.
#ifdef ENGINE_PHYSICS_ENABLED
#define PHYSICS_API CLOUD_ENGINE_API
#else
#define PHYSICS_API
#endif

// PhysicsEngineBridge — sole public API entry point into the physics subsystem
//
// [Thread-Safety Boundary]
//
//   This class is the ONLY coupling point between the main thread (MT) and the
//   physics thread (PT). External modules (Engine, networking, game logic, etc.)
//   MUST access the physics subsystem exclusively through this API.
//
//   Thread model:
//     - All public API methods are called from the main thread
//       (Engine::FrameLoop).
//     - Physics simulation runs on a dedicated physics thread
//       (PhysicsThread::EventLoop).
//     - MT <-> PT communication uses SPSC lock-free queues
//       (moodycamel::ConcurrentQueue). No mutexes are needed for
//       command/result passing.
//     - Synchronous queries (GetScriptVM, etc.) access data via Jolt
//       Physics' BodyLockInterface, which guarantees cross-thread safety.
//
//   [Runtime Thread Verification]
//
//   Most public API methods are restricted to the main thread. These methods
//   call VerifyMainThread() at entry, which checks that std::this_thread::get_id()
//   matches the thread that called Initialize(). In debug builds an assertion
//   fires; in all builds a warning is logged. This catches accidental cross-thread
//   calls during development and in production logs.
//
//   Methods WITH verification:  Initialize, Start, Shutdown, Tick, FetchResult,
//                                GetScriptVM, GetFixedDeltaTime
//   Methods WITHOUT verification: IsRunning, IsHealthy  (safe from any thread)
//     - Tick()         -> MT enqueues command into SPSC queue (lock-free)
//     - FetchResult()  -> MT dequeues result from SPSC queue (lock-free)
//     - IsRunning()    -> reads std::atomic<bool> (lock-free)
//     - IsHealthy()    -> reads std::atomic<bool> (lock-free)
//     - Initialize()   -> MT only, called before Start() (lifecycle isolated)
//     - Start()        -> MT only, launches physics thread (happens-before)
//     - Shutdown()     -> MT only, joins PT first then cleans up (happens-before)
//
// [External Module Constraint]
//
//   External modules MUST NOT directly include PhysicsSystem, PhysicsThread,
//   PhysicsScriptVM, PhysicsWorld, or any other internal physics header.
//   These headers are protected by the PHYSICS_INTERNAL_ACCESS macro —
//   including them without defining the macro will cause a compile error.
//
//   Correct:
//     #include "runtime/physics/physics_engine_bridge.h"  // only allowed include
//
//   Incorrect:
//     #include "runtime/physics/physics_system.h"          // compile error
//     #include "runtime/physics/physics_thread.h"          // compile error
//
// [Disabled-Module Support]
//
//   This header is always includable regardless of ENGINE_PHYSICS_ENABLED.
//   When the macro is off, all methods compile to zero-cost inline no-ops
//   and no physics code is linked. No #ifdef required in Engine.

#ifdef ENGINE_PHYSICS_ENABLED
#include "runtime/physics/physics_commands.h"
#endif

namespace engine {

class ScriptVM;

#ifdef ENGINE_PHYSICS_ENABLED
// (included above)
#else
// Stub type — ensures std::optional<PhysicsFrameResult> compiles
struct BodyTransform {
	uint32_t body_id = 0;
	double pos_x = 0.0, pos_y = 0.0, pos_z = 0.0;
	float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f, rot_w = 1.0f;
};
struct PhysicsFrameResult {
	uint64_t frame_id = 0;
	bool valid() const {
		return false;
	}
};
#endif	// ENGINE_PHYSICS_ENABLED

class PHYSICS_API PhysicsEngineBridge {
	public:
	PhysicsEngineBridge(const PhysicsEngineBridge&) = delete;
	PhysicsEngineBridge& operator=(const PhysicsEngineBridge&) = delete;

#ifdef ENGINE_PHYSICS_ENABLED

	// Full implementation (delegates to internal PhysicsSystem singleton)
	//
	// All methods below are called from the main thread. See the
	// thread-safety boundary comment at the top of this file for the
	// safety guarantees of each method.

	static PhysicsEngineBridge& Instance();

	// ── Lifecycle (MT, strict happens-before ordering with PT) ─────────

	/// Load configs, create physics-dedicated ScriptVM, load scripts.
	/// Does NOT start the physics thread.
	/// Thread: MT only. Must be called before Start().
	bool Initialize(const std::string& config_dir,
					const std::string& scripts_dir);

	// (deprecated) kept for ABI compatibility — assets_path is now
	// read from PhysicsConfig.scene_path.
	// (removed deprecated overload — assets_path now read from PhysicsConfig.scene_path)

	/// Start the physics thread. Physics simulation begins.
	/// Thread: MT only. Must be called after Initialize().
	bool Start();

	/// Stop the physics thread (join), destroy ScriptVM, release resources.
	/// Thread: MT only. After this call the physics thread is guaranteed
	/// to have exited (happens-before).
	void Shutdown();

	// ── Per-frame command enqueue (MT -> SPSC queue -> PT) ────────────

	/// Enqueue a Tick command. The physics thread will execute world_.Step(),
	/// trigger the PostStepCallback for Lua collision callbacks, then enqueue
	/// the result.
	/// Thread: MT only. Internal SPSC lock-free queue, thread-safe.
	bool Tick(uint64_t frame_id, float delta_time);

	/// Enqueue a dynamic body spawn command. The created body id is returned
	/// asynchronously in a later PhysicsFrameResult transform/diff packet.
	bool EnqueueSpawn(const std::string& proto_id,
					  double x,
					  double y,
					  double z,
					  float qx = 0.0f,
					  float qy = 0.0f,
					  float qz = 0.0f,
					  float qw = 1.0f,
					  uint64_t user_data = 0);
	bool EnqueueDestroy(uint32_t body_id);
	bool EnqueueApplyForce(
		uint32_t body_id, float fx, float fy, float fz, double px, double py, double pz);
	bool EnqueueSetVelocity(uint32_t body_id, float vx, float vy, float vz);

	/// Block until the physics frame result for the given frame_id is
	/// available, or timeout_ms elapses. Returns std::nullopt on timeout.
	/// Thread: MT only. Internal SPSC lock-free queue, thread-safe.
	std::optional<PhysicsFrameResult> FetchResult(uint64_t frame_id, int timeout_ms);

	// ── Status queries (read atomic variables, lock-free) ─────────────

	/// Whether the physics thread is currently running.
	/// Thread: any. Reads std::atomic<bool>, lock-free.
	bool IsRunning() const;

	/// Whether the physics thread is healthy (has not crashed).
	/// Thread: any. Reads std::atomic<bool>, lock-free.
	bool IsHealthy() const;

	/// Whether the physics system was initialized.
	/// Thread: any. Reads std::atomic<bool>, lock-free.
	bool IsInitialized() const;

	struct Vec3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct RayCastHit {
		uint32_t body_id = 0;
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	struct Stats {
		uint32_t active_bodies = 0;
		uint32_t total_bodies = 0;
		int body_pairs = 0;
		int contact_constraints = 0;
	};

	std::optional<BodyTransform> GetTransform(uint32_t body_id) const;
	std::optional<Vec3> GetVelocity(uint32_t body_id) const;
	bool IsBodyActive(uint32_t body_id) const;
	std::optional<RayCastHit> RayCast(
		double ox, double oy, double oz, double dx, double dy, double dz, float max_dist) const;
	Stats GetPhysicsStats() const;

	bool ReloadThresholds();
	bool ReloadLogLevel();
	bool Recover(const std::string& saved_state = {});

	// ── ScriptVM access ───────────────────────────────────────────────

	/// Get the physics-dedicated ScriptVM.
	/// Thread: MT may call. However, the returned VM's Lua state is
	/// exclusively accessed by the physics thread after Start()
	/// (coroutine driving, collision callbacks). The main thread
	/// should only use this during Initialize to register bindings
	/// or load scripts.
	ScriptVM* GetScriptVM();

	/// Get the fixed timestep for physics simulation.
	/// Thread: MT only. Reads a config snapshot, no contention.
	float GetFixedDeltaTime() const;

#else

	// Empty stubs (ENGINE_PHYSICS_ENABLED off — zero-cost inline no-ops)

	static PhysicsEngineBridge& Instance() {
		static PhysicsEngineBridge instance;
		return instance;
	}

	bool Initialize(const std::string&, const std::string&) {
		return true;
	}
	bool Start() {
		return true;
	}
	bool Tick(uint64_t, float) {
		return true;
	}
	bool EnqueueSpawn(const std::string&,
					  double,
					  double,
					  double,
					  float = 0.0f,
					  float = 0.0f,
					  float = 0.0f,
					  float = 1.0f,
					  uint64_t = 0) {
		return false;
	}
	bool EnqueueDestroy(uint32_t) {
		return false;
	}
	bool EnqueueApplyForce(uint32_t, float, float, float, double, double, double) {
		return false;
	}
	bool EnqueueSetVelocity(uint32_t, float, float, float) {
		return false;
	}
	std::optional<PhysicsFrameResult> FetchResult(uint64_t, int) {
		return std::nullopt;
	}
	void Shutdown() {
	}
	bool IsRunning() const {
		return false;
	}
	bool IsHealthy() const {
		return true;
	}
	bool IsInitialized() const {
		return true;
	}
	struct Vec3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};
	struct RayCastHit {
		uint32_t body_id = 0;
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};
	struct Stats {
		uint32_t active_bodies = 0;
		uint32_t total_bodies = 0;
		int body_pairs = 0;
		int contact_constraints = 0;
	};
	std::optional<BodyTransform> GetTransform(uint32_t) const {
		return std::nullopt;
	}
	std::optional<Vec3> GetVelocity(uint32_t) const {
		return std::nullopt;
	}
	bool IsBodyActive(uint32_t) const {
		return false;
	}
	std::optional<RayCastHit> RayCast(double, double, double, double, double, double, float) const {
		return std::nullopt;
	}
	Stats GetPhysicsStats() const {
		return {};
	}
	bool ReloadThresholds() {
		return false;
	}
	bool ReloadLogLevel() {
		return false;
	}
	bool Recover(const std::string& = {}) {
		return false;
	}
	ScriptVM* GetScriptVM() {
		return nullptr;
	}
	float GetFixedDeltaTime() const {
		return 0.01667f;
	}

#endif	// ENGINE_PHYSICS_ENABLED

	private:
	PhysicsEngineBridge() = default;
	~PhysicsEngineBridge() = default;

	// ── Thread verification ──────────────────────────────────────────
	//
	/// Captured at the start of Initialize() — the thread that bootstraps
	/// the physics subsystem is considered the "main thread". All MT-only
	/// methods assert/log if called from a different thread.
	std::thread::id main_thread_id_;

	/// Assert (debug) + log warning (all builds) if the calling thread
	/// is not the main thread that called Initialize().
	void VerifyMainThread() const;
};

}  // namespace engine
