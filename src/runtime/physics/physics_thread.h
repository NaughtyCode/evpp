#pragma once

//==============================================================================
// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// See physics_system.h for full documentation.
// Including this header without the macro will cause a compile-time #error.
//==============================================================================
#ifndef PHYSICS_INTERNAL_ACCESS
#error \
	"physics_thread.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <atomic>
#include <concurrentqueue.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <quill/Logger.h>

#include "runtime/physics/physics_commands.h"
#include "runtime/physics/physics_config.h"
#include "runtime/physics/physics_world.h"

namespace engine {

//==============================================================================
// PhysicsThread — dedicated physics thread [D18][D19][D20]
//
// [Thread Model]
//
//   Encapsulates a dedicated physics simulation thread and the communication
//   infrastructure with the main thread.
//
//   Thread architecture:
//     - Main thread (MT) enqueues commands via EnqueueCommand() into an
//       SPSC lock-free queue.
//     - Physics thread (PT) polls the command queue in EventLoop() and
//       executes commands.
//     - The physics thread returns results to the main thread through a
//       separate SPSC lock-free queue.
//     - No mutable state is shared: all communication goes through two
//       moodycamel::ConcurrentQueue (SPSC) instances. No mutexes needed.
//
//   Thread-safety analysis:
//     - command_queue_:  MT writes -> PT reads (SPSC, lock-free, thread-safe)
//     - result_queue_:   PT writes -> MT reads (SPSC, lock-free, thread-safe)
//     - running_ / healthy_: std::atomic<bool> (lock-free, thread-safe)
//     - world_:          PT-exclusive writes; MT only reads synchronously
//                        via Jolt BodyLockInterface (Jolt guarantees
//                        cross-thread safety)
//     - logger_:         Created in Start() (MT); thereafter PT exclusively
//                        writes log messages. MT reads the pointer value
//                        itself via GetLogger() (no contention)
//
//   PhysicsThread holds no mutexes. All thread safety relies on:
//     1. Correct use of SPSC lock-free queues (single producer, single consumer)
//     2. std::atomic acquire/release semantics
//     3. Jolt Physics BodyLockInterface
//
// [External Access Constraint]
//
//   This class is a physics subsystem implementation detail. External modules
//   MUST NOT use PhysicsThread directly. Compile-time protection is provided
//   by the PHYSICS_INTERNAL_ACCESS macro.
//==============================================================================

class PhysicsThread {
	public:
	PhysicsThread() = default;
	~PhysicsThread();

	PhysicsThread(const PhysicsThread&) = delete;
	PhysicsThread& operator=(const PhysicsThread&) = delete;

	// ── Lifecycle ────────────────────────────────────────────────────
	//
	// Start(): Called from MT. Creates independent logger, initializes
	//          PhysicsWorld, launches the EventLoop thread. Returns false
	//          if world initialization fails.
	// Stop():  Called from MT. Gracefully stops the physics thread by
	//          signaling via atomic flag + wakeup tick, then joins.
	//
	// Lifecycle is managed by PhysicsSystem; PhysicsSystem::Shutdown()
	// guarantees Stop() (join PT) is called before destroying related
	// resources.

	bool Start(const PhysicsConfig& config,
			   const ThreadingConfig& threading,
			   const ThresholdsConfig& thresholds,
			   const PhysicsLogConfig& log_config,
			   const std::string& assets_path);
	void Stop();

	// ── Main thread interface ────────────────────────────────────────
	//
	// The following methods are called from the main thread (via
	// PhysicsSystem / PhysicsEngineBridge).

	/// Enqueue a command for the physics thread. Returns false if the
	/// command queue is full (frame pile-up protection [D23]).
	/// Thread: MT. Enqueues into SPSC lock-free queue, thread-safe.
	bool EnqueueCommand(PhysicsCommand cmd);

	/// Try to dequeue a result from the result queue. Returns nullptr
	/// if the queue is empty.
	/// Thread: MT. Dequeues from SPSC lock-free queue, thread-safe.
	std::unique_ptr<PhysicsFrameResult> TryDequeueResult();

	// ── Health and status queries ────────────────────────────────────
	//
	/// Whether the physics thread has crashed [D21]. Reads std::atomic<bool>.
	bool IsHealthy() const {
		return healthy_.load(std::memory_order_acquire);
	}
	/// Whether the physics thread is currently running. Reads std::atomic<bool>.
	bool IsRunning() const {
		return running_.load(std::memory_order_acquire);
	}

	// ── Thread verification ─────────────────────────────────────────
	//
	/// Captured at the start of EventLoop(). Used to verify that
	/// PhysicsSystem::UpdateScript() (and any future PT-only code)
	/// is actually executing on the physics thread.
	///
	/// In debug builds: fires an assert if the calling thread is not
	/// the physics thread. In all builds: logs an error.
	void VerifyIsPhysicsThread() const;

	// ── Logger access ────────────────────────────────────────────────
	//
	/// Returns the logger created in Start(), or nullptr before Start() /
	/// after Stop().
	/// Thread: MT can safely read the pointer value (PT only writes log
	/// messages through it, never modifies the pointer itself).
	quill::Logger* GetLogger() const {
		return logger_;
	}

	// ── PhysicsWorld access (cross-thread-safe queries) ──────────────
	//
	/// Get the PhysicsWorld reference for synchronous queries.
	/// Thread: MT. Query methods internally use Jolt's BodyLockInterface,
	/// which guarantees cross-thread safety.
	PhysicsWorld& GetWorld() {
		return world_;
	}
	const PhysicsWorld& GetWorld() const {
		return world_;
	}

	// ── Save / Restore physics state (delegates to PhysicsWorld) ─────
	std::string SaveState() const {
		return world_.SaveState();
	}
	bool RestoreState(const std::string& data) {
		return world_.RestoreState(data);
	}

	// ── Hot-reload thresholds ────────────────────────────────────────
	void SetThresholds(const ThresholdsConfig& thresholds) {
		world_.SetThresholds(thresholds);
		thresholds_config_ = thresholds;
	}

	// ── PostStepCallback — invoked on PT after each world_.Step() ────
	//
	/// Called on the physics thread after each successful world_.Step(),
	/// receiving that frame's collision event list.
	/// Used by PhysicsSystem to drive Lua collision callbacks
	/// (on_physics_collision).
	///
	/// Call site: PhysicsThread EventLoop, after world_.Step(), before
	///            the result is enqueued.
	/// Thread:    PT (callback executes on physics thread, exclusive
	///            access to Lua state).

	using PostStepCallback = std::function<void(const std::vector<CollisionEvent>&)>;
	void SetPostStepCallback(PostStepCallback cb) {
		post_step_callback_ = std::move(cb);
	}

	// ── Crash recovery ───────────────────────────────────────────────
	//
	/// Recover after a physics thread crash. Stops the old thread (if
	/// still partially running), restarts with the same config, and
	/// optionally restores world state.
	/// Returns false on failure (lost config, world init failure).

	bool Recover(const std::string& saved_state = {});

	private:
	// ── EventLoop — runs on the dedicated physics thread ─────────────
	//
	/// Main loop of the physics thread. Polls the command queue, executes
	/// commands (Spawn, Destroy, ApplyForce, SetVelocity, Tick), steps
	/// the physics world on Tick, triggers the PostStepCallback, and
	/// enqueues results.
	///
	/// Thread: PT only. All member access within this function is
	/// single-threaded — no concurrency to consider.

	void EventLoop();

	// ── Create independent logger instance [D7][D8] ──────────────────
	quill::Logger* CreatePhysicsLogger(const PhysicsLogConfig& log_config);

	// ═════════════════════════════════════════════════════════════════
	// Members
	// ═════════════════════════════════════════════════════════════════
	//
	// Thread ownership tags:
	//   [MT]   = main-thread-exclusive (via PhysicsSystem/PhysicsEngineBridge)
	//   [PT]   = physics-thread-exclusive (within EventLoop)
	//   [MT->] = MT initializes, PT reads (happens-before guarantee)
	//   [SPSC] = MT and PT communicate via SPSC queue (lock-free)
	//   [ATOM] = std::atomic, lock-free read/write

	PhysicsWorld world_;  // [PT] PT-exclusive writes
	// [MT] MT reads only via
	// BodyLockInterface

	std::unique_ptr<std::thread> thread_;  // [MT] lifecycle management

	// SPSC lock-free queues (moodycamel::ConcurrentQueue)
	// Each queue has exactly one producer and one consumer.
	moodycamel::ConcurrentQueue<PhysicsCommand> command_queue_;	 // [SPSC] MT -> PT
	moodycamel::ConcurrentQueue<PhysicsFrameResult> result_queue_;	// [SPSC] PT -> MT

	// Atomic flags (acquire/release semantics, lock-free)
	std::atomic<bool> running_{false};	// [ATOM] MT writes, PT reads
	std::atomic<bool> healthy_{false};	// [ATOM] PT writes, MT reads

	// Config snapshots (captured at Start(), consumed by EventLoop / Recover)
	PhysicsConfig physics_config_;	// [MT->] immutable after Start
	ThreadingConfig threading_config_;	// [MT->] immutable after Start
	ThresholdsConfig thresholds_config_;  // [MT->] updatable via
	// SetThresholds
	PhysicsLogConfig log_config_;  // [MT->] immutable after Start
	std::string assets_path_;  // [MT->] immutable after Start

	// Independent logger (owned by physics thread)
	// logger_ pointer created in Start() (MT); thereafter only PT writes
	// log messages through it. MT reads the pointer value via GetLogger()
	// (no contention on the pointer itself).
	quill::Logger* logger_ = nullptr;  // [MT->] pointer value stable;
	// [PT] log writes

	// PostStepCallback (executes on physics thread, PT-exclusive)
	PostStepCallback post_step_callback_;  // [MT->] set in Start();
	// [PT] invoked in EventLoop

	// Physics thread ID — captured at EventLoop() entry, used by
	// VerifyIsPhysicsThread() to detect cross-thread misuse.
	std::thread::id physics_thread_id_;	 // [PT] set once in EventLoop
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
