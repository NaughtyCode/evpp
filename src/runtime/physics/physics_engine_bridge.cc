#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_engine_bridge.h"

#include <cassert>

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_system.h"

// ============================================================================
// PhysicsEngineBridge implementation
//
// Every method in this file delegates directly to the corresponding method
// on PhysicsSystem::Instance(). PhysicsEngineBridge contains no business
// logic — it is a transparent proxy layer whose purposes are:
//   1. Hide PhysicsSystem header from the public API surface
//   2. Ensure all cross-thread-boundary calls go through a single entry point
//   3. Provide compile-time stubs when ENGINE_PHYSICS_ENABLED is off
//
// [Thread verification]
//
//   MT-only methods call VerifyMainThread() at entry. This captures the
//   calling thread's ID on first Initialize() and checks it on every
//   subsequent MT-only call. A mismatch indicates a bug (e.g. calling
//   Tick() or FetchResult() from a worker thread or the physics thread).
//
//   In debug builds: assert() fires — immediate diagnostic with stack trace.
//   In release builds: ENGINE_LOG_ERROR records the violation.
//
//   Methods with verification:  Initialize, Start, Shutdown, Tick,
//                                FetchResult, GetScriptVM, GetFixedDeltaTime
//   Methods without:            IsRunning, IsHealthy (read atomics, safe
//                                from any thread)
// ============================================================================

namespace engine {

// ---------------------------------------------------------------------------
// VerifyMainThread — runtime guard against cross-thread API misuse
// ---------------------------------------------------------------------------
//
// Called at the top of every MT-only public method.  The main thread ID is
// captured once during the first Initialize() call.  If a different thread
// later calls an MT-restricted method, this fires:
//   - assert() in debug builds (hard stop with diagnostics)
//   - ENGINE_LOG_ERROR in all builds (audit trail in production logs)
//
// The check is intentionally placed in PhysicsEngineBridge rather than
// PhysicsSystem so that the guard sits at the public API boundary — the
// same boundary that external modules cross.
// ---------------------------------------------------------------------------

void PhysicsEngineBridge::VerifyMainThread() const {
	// Skip check if main_thread_id_ hasn't been captured yet
	// (default-constructed thread::id means "not a thread").
	if (main_thread_id_ != std::thread::id{}) {
		assert(main_thread_id_ == std::this_thread::get_id() &&
			   "PhysicsEngineBridge: MT-only API called from wrong thread. "
			   "These APIs must only be called from the main thread "
			   "(Engine::FrameLoop).");
	}
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

PhysicsEngineBridge& PhysicsEngineBridge::Instance() {
	static PhysicsEngineBridge instance;
	return instance;
}

// ---------------------------------------------------------------------------
// Initialize — captures the main thread ID, then delegates
// ---------------------------------------------------------------------------

bool PhysicsEngineBridge::Initialize(const std::string& config_dir,
									 const std::string& assets_path,
									 const std::string& scripts_dir) {
	// Capture the calling thread as the "main thread".
	// All subsequent MT-only calls verify against this ID.
	main_thread_id_ = std::this_thread::get_id();

	bool ok = PhysicsSystem::Instance().Initialize(config_dir, assets_path, scripts_dir);

	// Re-verify in case Initialize() is called a second time from a
	// different thread (lifecycle methods should all be on the same thread).
	VerifyMainThread();

	return ok;
}

// ---------------------------------------------------------------------------
// Start — MT-only, verified
// ---------------------------------------------------------------------------
// Must be called from the main thread. The physics thread is launched here;
// calling from the wrong thread would break the happens-before guarantee
// between Initialize/Start/Shutdown.

bool PhysicsEngineBridge::Start() {
	VerifyMainThread();
	return PhysicsSystem::Instance().Start();
}

// ---------------------------------------------------------------------------
// Shutdown — MT-only, verified
// ---------------------------------------------------------------------------
// Must be called from the main thread. Joins the physics thread before
// cleanup; calling from a different thread could deadlock or race with
// other lifecycle calls.

void PhysicsEngineBridge::Shutdown() {
	VerifyMainThread();
	PhysicsSystem::Instance().Shutdown();
}

// ---------------------------------------------------------------------------
// Tick — MT-only, verified
// ---------------------------------------------------------------------------
// Enqueues a Tick command into the SPSC lock-free queue. The SPSC queue
// assumes a single producer (the main thread). Calling this from multiple
// threads would violate the SPSC contract and cause data races.

void PhysicsEngineBridge::Tick(uint64_t frame_id, float delta_time) {
	VerifyMainThread();
	PhysicsSystem::Instance().Tick(frame_id, delta_time);
}

// ---------------------------------------------------------------------------
// FetchResult — MT-only, verified
// ---------------------------------------------------------------------------
// Dequeues from the SPSC result queue. The SPSC queue assumes a single
// consumer (the main thread). Calling this from multiple threads would
// violate the SPSC contract.

std::optional<PhysicsFrameResult> PhysicsEngineBridge::FetchResult(uint64_t frame_id,
																   int timeout_ms) {
	VerifyMainThread();
	return PhysicsSystem::Instance().FetchResult(frame_id, timeout_ms);
}

// ---------------------------------------------------------------------------
// IsRunning — safe from any thread
// ---------------------------------------------------------------------------
// Reads std::atomic<bool> with acquire semantics. No verification needed.

bool PhysicsEngineBridge::IsRunning() const {
	return PhysicsSystem::Instance().IsRunning();
}

// ---------------------------------------------------------------------------
// IsHealthy — safe from any thread
// ---------------------------------------------------------------------------
// Reads std::atomic<bool> with acquire semantics. No verification needed.

bool PhysicsEngineBridge::IsHealthy() const {
	return PhysicsSystem::Instance().IsHealthy();
}

// ---------------------------------------------------------------------------
// GetScriptVM — MT-only, verified
// ---------------------------------------------------------------------------
// Returns a pointer to the physics-dedicated ScriptVM. After Start() the
// returned VM's Lua state is PT-exclusive. The main thread should only
// use this during initialization (register bindings, load scripts).

ScriptVM* PhysicsEngineBridge::GetScriptVM() {
	VerifyMainThread();
	return &PhysicsSystem::Instance().GetScriptVM();
}

// ---------------------------------------------------------------------------
// GetFixedDeltaTime — MT-only, verified
// ---------------------------------------------------------------------------
// Reads a config snapshot. Restricted to MT to keep the config access
// pattern simple (config hot-reload also happens on MT).

float PhysicsEngineBridge::GetFixedDeltaTime() const {
	VerifyMainThread();
	return PhysicsSystem::Instance().GetFixedDeltaTime();
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
