// NOMINMAX must be defined before any windows.h inclusion,
// which can come via engine.h -> invoke_timer.h -> ...
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include "runtime/engine/engine.h"

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/event_watcher.h>
#include <runtime/evpp/invoke_timer.h>

#include "runtime/core/log/log.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/physics/physics_engine_bridge.h"
#include "runtime/profiler/profiler_core.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/script_bind.h"
#include "runtime/vm/vm.h"

namespace engine {

Engine& Engine::Instance() {
    static Engine instance;
    return instance;
}

Engine::Engine() = default;
Engine::~Engine() {
    Cleanup();
}

ScriptVM& Engine::GetScriptVM() {
    if (!script_vm_) {
        std::fprintf(stderr, "FATAL: GetScriptVM() called before Engine::Init()\n");
        abort();
    }
    return *script_vm_;
}

//============================================================================
// Init
//============================================================================

void Engine::Init(const RuntimeConfig& runtime_cfg,
                  const std::string& entry_scripts_dir,
                  evpp::EventLoop* external_loop) {
    std::fprintf(stderr, "[engine] Init() begin\n");
    std::fprintf(stderr, "[engine] InitLogger...\n");
    InitLogger(runtime_cfg.log);

    auto* logger = GetLogger();
    std::fprintf(stderr, "[engine] logger created\n");

    // ── Profiler initialization ────────────────────────────────────────
    {
        ProfilerConfig prof_cfg;
        prof_cfg.buffer_size_kb = 32768;
        ProfilerManager::Get().Initialize(prof_cfg);
        ProfilerManager::Get().StartSession();
        ENGINE_LOG_INFO(logger, "profiler initialized and session started, "
                        "enabled=[{}]", ProfilerManager::IsEnabled());
    }
    std::fprintf(stderr, "[engine] profiler initialized\n");

    ENGINE_PROFILE_SCOPE("engine", "Init");

    ENGINE_LOG_INFO(logger,
                    "engine initializing, resource_dir=[{}], "
                    "log_dir=[{}], log_level=[{}], "
                    "runtime_scripts_dir=[{}], entry_scripts_dir=[{}], "
                    "frame_interval=[{}ms], library_mode=[{}]",
                    runtime_cfg.resource_dir,
                    runtime_cfg.log.dir, runtime_cfg.log.level,
                    runtime_cfg.scripts_dir, entry_scripts_dir,
                    runtime_cfg.frame.interval_ms,
                    (external_loop != nullptr));

    std::fprintf(stderr, "[engine] creating TimerManager...\n");
    TimerManager::create_instance();
    ENGINE_LOG_INFO(logger, "timer manager initialized");

    if (external_loop) {
        loop_ = external_loop;
        running_ = true;  // library mode: engine is immediately "running"
    } else {
        std::fprintf(stderr, "[engine] creating EventLoop...\n");
        owned_loop_ = std::make_unique<evpp::EventLoop>();
        loop_ = owned_loop_.get();
        std::fprintf(stderr, "[engine] EventLoop created\n");
    }
    if (runtime_cfg.frame.target_fps > 0) {
        frame_interval_ = std::chrono::milliseconds(1000 / runtime_cfg.frame.target_fps);
    } else {
        frame_interval_ = std::chrono::milliseconds(runtime_cfg.frame.interval_ms);
    }

    script_vm_ = std::make_unique<ScriptVM>();
    ENGINE_LOG_INFO(logger, "lua vm initialized, version=[{}]", ScriptVM::LuaVersion());
    std::fprintf(stderr, "[engine] ScriptVM created\n");

    // ── Physics system initialization ──────────────────────────────────
    {
        std::fprintf(stderr, "[engine] initializing physics...\n");
        auto phys_cfg = runtime_cfg.resource_dir + "/physics/configs";
        auto phys_data = runtime_cfg.resource_dir + "/physics/data/scene.json";
        bool ok = PhysicsEngineBridge::Instance().Initialize(
            phys_cfg, phys_data, runtime_cfg.scripts_dir);
        if (!ok) {
            ENGINE_LOG_WARN(logger, "physics system failed to initialize");
        } else {
            fixed_delta_time_ = PhysicsEngineBridge::Instance().GetFixedDeltaTime();
            ENGINE_LOG_INFO(logger, "physics system initialized, fixed_delta_time=[{}s]",
                            fixed_delta_time_);
        }
        std::fprintf(stderr, "[engine] physics init done (ok=%d)\n", ok);
    }

    // Set import search path to the shared scripts root (parent of runtime/,
    // client/, server/) so that import("runtime.init") resolves from both
    // client and server entry scripts.
    {
        std::string scripts_root =
            std::filesystem::path(runtime_cfg.scripts_dir).parent_path().string();
        if (scripts_root.empty()) {
            scripts_root = ".";
        }
        script_vm_->SetImportPath(scripts_root);
    }
    script::ExportAll(*script_vm_);

    last_frame_time_ = std::chrono::steady_clock::now();
    last_work_time_ = last_frame_time_;

    if (!entry_scripts_dir.empty()) {
        std::fprintf(stderr, "[engine] loading scripts from [%s]...\n", entry_scripts_dir.c_str());
        size_t failed = script_vm_->DoDirectory(entry_scripts_dir);
        if (failed > 0) {
            ENGINE_LOG_WARN(logger, "scripts dir [{}]: [{}] file(s) failed to load",
                            entry_scripts_dir, failed);
        }
        script_vm_->InitScript();
        std::fprintf(stderr, "[engine] scripts loaded\n");
    }

    std::fprintf(stderr, "[engine] Init() complete\n");
}

//============================================================================
// Start — standalone mode: arm frame timer and signal watchers
//============================================================================

void Engine::Start() {
    std::fprintf(stderr, "[engine] Start() begin\n");
    ENGINE_PROFILE_SCOPE("engine", "Start");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "engine starting, frame_interval=[{}ms]",
                    frame_interval_.count());

    // ── Start physics simulation ──────────────────────────────────────
    // Must be called after Initialize() and before the first Tick().
    // If Initialize() failed, Start() is a safe no-op.
    PhysicsEngineBridge::Instance().Start();

#ifndef _WIN32
    sigint_watcher_ = std::make_unique<evpp::SignalEventWatcher>(
        SIGINT, loop_, [this]() {
            auto* logger = GetLogger();
            ENGINE_LOG_INFO(logger, "SIGINT received, shutting down...");
            Shutdown();
        });
    if (!sigint_watcher_->Init() || !sigint_watcher_->AsyncWait()) {
        ENGINE_LOG_ERROR(logger, "failed to initialize SIGINT watcher");
    }

    sigterm_watcher_ = std::make_unique<evpp::SignalEventWatcher>(
        SIGTERM, loop_, [this]() {
            auto* logger = GetLogger();
            ENGINE_LOG_INFO(logger, "SIGTERM received, shutting down...");
            Shutdown();
        });
    if (!sigterm_watcher_->Init() || !sigterm_watcher_->AsyncWait()) {
        ENGINE_LOG_ERROR(logger, "failed to initialize SIGTERM watcher");
    }
#endif

    frame_timer_ = loop_->RunEvery(
        evpp::Duration(frame_interval_.count() * evpp::Duration::kMillisecond),
        [this]() { Tick(); });

    running_ = true;
    last_frame_time_ = std::chrono::steady_clock::now();
    last_work_time_ = last_frame_time_;
    std::fprintf(stderr, "[engine] Start() complete, running_=true\n");
}

//============================================================================
// Run — standalone convenience: Start + dispatch + Cleanup
//============================================================================

void Engine::Run() {
    std::fprintf(stderr, "[engine] Run() begin, calling Start()\n");
    Start();

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "entering main loop");
    std::fprintf(stderr, "[engine] entering main loop (loop_->Run())\n");
    loop_->Run();
    ENGINE_LOG_INFO(logger, "main loop exited, frame_count=[{}]", frame_count_);
    std::fprintf(stderr, "[engine] main loop exited\n");

    Cleanup();
}

//============================================================================
// Tick — one frame of engine work
//============================================================================

void Engine::Tick() {
    if (!running_) return;

    auto now = std::chrono::steady_clock::now();
    if (now - last_work_time_ < frame_interval_) return;

    { ENGINE_PROFILE_TICK();
    FrameLoop();
    last_work_time_ = std::chrono::steady_clock::now();
    }  // Tick slice ends
}

//============================================================================
// Shutdown — request graceful stop
//============================================================================

void Engine::Shutdown() {
    ENGINE_PROFILE_SCOPE("engine", "Shutdown");

    if (running_.exchange(false)) {
        // Only stop the engine's own loop. In library mode the host
        // manages the external EventLoop lifetime.
        if (owned_loop_) {
            owned_loop_->Stop();
        }
    }
}

//============================================================================
// Cleanup — release all resources
//============================================================================

void Engine::Cleanup() {
    ENGINE_PROFILE_SCOPE("engine", "Cleanup");

    if (cleaned_up_.exchange(true)) return;

    // Shutdown physics (stops thread + destroys physics VM) — before engine VM
    PhysicsEngineBridge::Instance().Shutdown();

    // Cancel frame timer before destroying Lua state.
    if (frame_timer_) {
        frame_timer_->Cancel();
        frame_timer_.reset();
    }

    script::ShutdownNetBindings();
    if (script_vm_) {
        script::ShutdownTimerBindings(*script_vm_);
    }

    if (script_vm_) {
        script_vm_->DestroyScript();
        int mem_kb = lua_gc(script_vm_->GetState(), LUA_GCCOUNT, 0);
        auto* logger = GetLogger();
        ENGINE_LOG_INFO(logger, "ScriptVM: final memory [{} KB], exiting", mem_kb);
    }
    TimerManager::destroy_instance();

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "timer manager shut down");

    // Release signal watchers.
    sigint_watcher_.reset();
#ifndef _WIN32
    sigterm_watcher_.reset();
#endif

    // ── Profiler shutdown ──────────────────────────────────────────────
    // Must happen after all subsystems stop (physics, timers, VM)
    // and before the logger is destroyed, so profiler can log its status.
    {
        ENGINE_LOG_INFO(logger, "profiler: flushing, stopping, saving trace...");
        ProfilerManager::Get().Flush();
        ProfilerManager::Get().StopSession();
        ProfilerManager::Get().SaveTrace();
        ProfilerManager::Get().Shutdown();
    }

    ShutdownLogger();

    loop_ = nullptr;
}

//============================================================================
// FrameLoop — per-frame work (timer update + Lua update)
//============================================================================

void Engine::FrameLoop() {
    if (!running_) return;

    auto frame_start = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        frame_start - last_frame_time_);
    last_frame_time_ = frame_start;

    ++frame_count_;

    ENGINE_PROFILE_FRAME_BEGIN(frame_count_, elapsed.count());

    { ENGINE_PROFILE_TIMER_UPDATE();
    TimerManager::instance().update();
    }  // TimerUpdate slice ends

    // [D17.1][D17.2] Trigger physics simulation (Tick enqueues command; physics thread steps)
    { ENGINE_PROFILE_PHYSICS_TICK(frame_count_);
    PhysicsEngineBridge::Instance().Tick(frame_count_, fixed_delta_time_);
    }  // PhysicsTick slice ends

    // [D17.3] Engine Lua script update + non-physics tasks
    { ENGINE_PROFILE_SCRIPT_UPDATE();
    if (script_vm_) {
        script_vm_->UpdateScript();
    }
    }  // ScriptUpdate slice ends

    // [D17.4] Fetch physics result for this frame
    { ENGINE_PROFILE_PHYSICS_FETCH(frame_count_);
    auto result = PhysicsEngineBridge::Instance().FetchResult(frame_count_, 5);
    if (result) {
        // [D17.5] Game object state update from result->transforms would go here
        // [D17.6] Physics VM collision callbacks — after FetchResult
        { ENGINE_PROFILE_SCRIPT_CALLBACK();
        PhysicsEngineBridge::Instance().UpdateScript();
        }  // ScriptCallback slice ends
        // [D17.7] Network sync construction from result->diff_packets would go here
    }
    }  // PhysicsFetch slice ends

    auto frame_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - frame_start).count();
    ENGINE_PROFILE_FRAME_END(frame_elapsed);

    // Slow frame detection
    ENGINE_PROFILE_SLOW_FRAME(frame_elapsed, frame_interval_.count() * 2);

    if (elapsed > frame_interval_ * 2) {
        if (frame_count_ - last_slow_frame_log_ > 30) {
            auto* logger = GetLogger();
            ENGINE_LOG_DEBUG(logger,
                             "frame [{}] took [{}ms] (slow)",
                             frame_count_, elapsed.count());
            last_slow_frame_log_ = frame_count_;
        }
    }
}

} // namespace engine
