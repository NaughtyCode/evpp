#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <runtime/evpp/invoke_timer.h>

#include "runtime/config/config.h"
#include "runtime/core/engine_api.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/monitoring/admin_http.h"

namespace evpp {
class EventLoop;
class SignalEventWatcher;
}

namespace engine {

class ScriptVM;
class ScriptReloader;
struct PhysicsFrameResult;

// Callback invoked each frame with physics simulation results.
// Registered by the game layer to consume transforms, collision events,
// and diff packets produced by the physics thread.
using PhysicsResultHandler = std::function<void(const PhysicsFrameResult&)>;

class ENGINE_API Engine {
	public:
	static Engine& Instance();

	// Test support: inject a test Engine instance. When set,
	// Instance() returns *test_instance instead of the global
	// singleton, enabling test isolation without full engine startup.
	static void SetInstanceForTesting(Engine* test_instance);
	static void ClearTestInstance();

	/*
	 * Cleanup Lifecycle Order (MUST be maintained):
	 *
	 *   1. Physics Shutdown      — stops physics VM + physics thread
	 *   2. Database Shutdown     — stops DB threads + drains queues
	 *   3. Network Shutdown      — [REQUIRES script_vm_ alive]
	 *   4. Timer Shutdown        — [REQUIRES script_vm_ alive]
	 *   5. DestroyScript         — destroys Lua VM + lifecycle hooks
	 *   6. Final Logs            — GC stats, cleanup confirmation
	 *
	 * CRITICAL: Steps 3-4 require script_vm_ to be alive.
	 * Do NOT reorder without updating ALL callers.
	 */
	enum class CleanupPhase {
		NotStarted,
		PhysicsShutdown,
		DatabaseShutdown,
		NetworkShutdown,
		TimerShutdown,
		ScriptDestroyed,
		FinalLogs,
		Complete
	};

	Engine();
	~Engine();

	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	// Initialize the engine.
	// runtime_cfg provides resource_dir, log, frame, and scripts_dir settings.
	// entry_scripts_dir is the role-specific scripts directory
	// (e.g. resources/script/server or resources/script/client).
	// In library mode, pass the host's EventLoop; the engine will use it
	// for all async operations but will not own it.
	void Init(const RuntimeConfig& runtime_cfg,
			  const std::string& entry_scripts_dir,
			  evpp::EventLoop* external_loop = nullptr);

	// ── Standalone mode ──────────────────────────────────────────────

	// Arm the frame timer and signal watchers on the engine's own loop.
	// Must be called after Init().  Does NOT enter the event loop.
	void Start();

	// Convenience: Start() + enter event loop + Cleanup().
	// Blocks until Shutdown() is called.  Standalone mode only.
	void Run();

	// ── Library mode ─────────────────────────────────────────────────
	// When an external EventLoop is provided to Init(), the host
	// application drives the engine:
	//
	//   engine.Init(config, &my_loop);
	//   while (running) {
	//       my_loop_dispatch_pending();   // host drives IO
	//       engine.Tick();                // per-frame engine work
	//   }
	//   engine.Cleanup();
	//

	// Process one frame: timer update + Lua update.
	// Enforces frame rate limit — if called faster than target_fps
	// (or interval_ms), the call is a no-op.
	// In standalone mode this is called by the frame timer.
	// In library mode the host calls this at its own cadence.
	void Tick();

	// ── Shutdown ─────────────────────────────────────────────────────

	// Request graceful shutdown. Safe to call from any thread.
	void Shutdown();

	// Release all resources (Lua, timers, net bindings, timer manager).
	// In standalone mode this is called automatically after the event
	// loop exits.  In library mode the host must call this before
	// destroying the engine.
	void Cleanup();

	// Apply pending config changes on the main thread.
	// Called automatically from FrameLoop() when config changes are
	// detected via reload callback.
	void ApplyConfigChanges();

	// ── Accessors ────────────────────────────────────────────────────

	bool running() const {
		return running_;
	}
	uint64_t frame_count() const {
		return frame_count_.load(std::memory_order_relaxed);
	}
	bool initialized() const {
		return initialized_.load(std::memory_order_acquire);
	}
	CleanupPhase cleanup_phase() const {
		return cleanup_phase_.load(std::memory_order_acquire);
	}

	ScriptVM& GetScriptVM();
	TimerManager& GetTimerManager() { return *timer_mgr_; }
	evpp::EventLoop* GetEventLoop() const {
		return loop_;
	}

	// Register a callback to consume physics results each frame.
	// When set, Engine::FrameLoop invokes this after FetchResult.
	void SetPhysicsResultHandler(PhysicsResultHandler handler);

	private:
	void FrameLoop();

	// The active event loop — either owned_loop_ or an external one.
	evpp::EventLoop* loop_ = nullptr;
	std::unique_ptr<evpp::EventLoop> owned_loop_;

	std::chrono::steady_clock::time_point last_frame_time_;
	std::chrono::steady_clock::time_point last_work_time_;
	std::chrono::milliseconds frame_interval_{33};
	std::atomic<bool> cleaned_up_{false};
	std::atomic<bool> running_{false};
	std::atomic<uint64_t> frame_count_{0};
	uint64_t last_slow_frame_log_{0};

	float fixed_delta_time_{0.01667f};	// physics fixed timestep (from PhysicsConfig or default)

	PhysicsResultHandler physics_result_handler_;

	std::unique_ptr<TimerManager> timer_mgr_;
	std::unique_ptr<ScriptVM> script_vm_;
	std::unique_ptr<ScriptReloader> script_reloader_;
	std::atomic<bool> initialized_{false};
	std::atomic<CleanupPhase> cleanup_phase_{CleanupPhase::NotStarted};

	monitoring::AdminHttpServer admin_server_;

	// Standalone-mode resources (owned, created in Start, destroyed in Cleanup).
	std::unique_ptr<evpp::SignalEventWatcher> sigint_watcher_;
#ifndef _WIN32
	std::unique_ptr<evpp::SignalEventWatcher> sigterm_watcher_;
	std::unique_ptr<evpp::SignalEventWatcher> sighup_watcher_;
#endif
	evpp::InvokeTimerPtr frame_timer_;

	// Pending config changes for main-thread application.
	std::mutex pending_config_mutex_;
	ConfigChangeSet pending_config_changes_;
	std::atomic<bool> config_changes_pending_{false};
	std::string config_dir_;
};

}  // namespace engine
