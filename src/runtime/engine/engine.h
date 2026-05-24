#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <runtime/evpp/invoke_timer.h>

#include "runtime/config/config.h"
#include "runtime/core/engine_export.h"

namespace evpp {
class EventLoop;
class SignalEventWatcher;
}

namespace engine {

class ScriptVM;

class ENGINE_API Engine {
public:
    static Engine& Instance();

    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Initialize the engine. In library mode, pass the host's EventLoop;
    // the engine will use it for all async operations but will not own it.
    void Init(const EngineConfig& config,
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

    // ── Accessors ────────────────────────────────────────────────────

    bool running() const { return running_; }
    uint64_t frame_count() const { return frame_count_; }

    ScriptVM& GetScriptVM();
    evpp::EventLoop* GetEventLoop() const { return loop_; }

private:
    void FrameLoop();

    // The active event loop — either owned_loop_ or an external one.
    evpp::EventLoop* loop_ = nullptr;
    std::unique_ptr<evpp::EventLoop> owned_loop_;

    std::chrono::steady_clock::time_point last_frame_time_;
    std::chrono::steady_clock::time_point last_work_time_;
    std::chrono::milliseconds frame_interval_{33};
    bool cleaned_up_{false};
    bool running_{false};
    uint64_t frame_count_{0};
    uint64_t last_slow_frame_log_{0};

    float fixed_delta_time_{0.01667f};  // physics fixed timestep (from PhysicsConfig or default)

    std::unique_ptr<ScriptVM> script_vm_;

    // Standalone-mode resources (owned, created in Start, destroyed in Cleanup).
    std::unique_ptr<evpp::SignalEventWatcher> sigint_watcher_;
#ifndef _WIN32
    std::unique_ptr<evpp::SignalEventWatcher> sigterm_watcher_;
#endif
    evpp::InvokeTimerPtr frame_timer_;
};

} // namespace engine
