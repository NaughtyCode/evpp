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

#include "engine/engine/engine.h"

#include <csignal>
#include <cstdio>
#include <memory>
#include <thread>

#include <evpp/event_loop.h>
#include <evpp/event_watcher.h>
#include <evpp/invoke_timer.h>

#include "engine/core/log/log.h"
#include "engine/core/timer/timer_manager.h"
#include "engine/script/script_bind.h"
#include "engine/vm/vm.h"

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

void Engine::Init(const EngineConfig& config, evpp::EventLoop* external_loop) {
    InitLogger(config.log);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger,
                    "engine initializing, log_dir=[{}], log_level=[{}], "
                    "scripts_dir=[{}], frame_interval=[{}ms], library_mode=[{}]",
                    config.log.dir, config.log.level,
                    config.scripts_dir, config.frame.interval_ms,
                    (external_loop != nullptr));

    TimerManager::create_instance();
    ENGINE_LOG_INFO(logger, "timer manager initialized");

    if (external_loop) {
        loop_ = external_loop;
        running_ = true;  // library mode: engine is immediately "running"
    } else {
        owned_loop_ = std::make_unique<evpp::EventLoop>();
        loop_ = owned_loop_.get();
    }
    if (config.frame.target_fps > 0) {
        frame_interval_ = std::chrono::milliseconds(1000 / config.frame.target_fps);
    } else {
        frame_interval_ = std::chrono::milliseconds(config.frame.interval_ms);
    }

    script_vm_ = std::make_unique<ScriptVM>();
    ENGINE_LOG_INFO(logger, "lua vm initialized, version=[{}]", ScriptVM::LuaVersion());

    script_vm_->SetImportPath(config.scripts_dir);
    script::ExportAll(*script_vm_);

    last_frame_time_ = std::chrono::steady_clock::now();
    last_work_time_ = last_frame_time_;

    if (!config.scripts_dir.empty()) {
        size_t failed = script_vm_->DoDirectory(config.scripts_dir);
        if (failed > 0) {
            ENGINE_LOG_WARN(logger, "scripts dir [{}]: [{}] file(s) failed to load",
                            config.scripts_dir, failed);
        }
        script_vm_->InitScript();
    }
}

//============================================================================
// Start — standalone mode: arm frame timer and signal watchers
//============================================================================

void Engine::Start() {
    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "engine starting, frame_interval=[{}ms]",
                    frame_interval_.count());

    sigint_watcher_ = std::make_unique<evpp::SignalEventWatcher>(
        SIGINT, loop_, [this]() {
            ENGINE_LOG_INFO(GetLogger(), "SIGINT received, shutting down...");
            Shutdown();
        });
    if (!sigint_watcher_->Init() || !sigint_watcher_->AsyncWait()) {
        ENGINE_LOG_ERROR(logger, "failed to initialize SIGINT watcher");
    }

#ifndef _WIN32
    sigterm_watcher_ = std::make_unique<evpp::SignalEventWatcher>(
        SIGTERM, loop_, [this]() {
            ENGINE_LOG_INFO(GetLogger(), "SIGTERM received, shutting down...");
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
}

//============================================================================
// Run — standalone convenience: Start + dispatch + Cleanup
//============================================================================

void Engine::Run() {
    Start();

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "entering main loop");
    loop_->Run();
    ENGINE_LOG_INFO(logger, "main loop exited, frame_count=[{}]", frame_count_);

    Cleanup();
}

//============================================================================
// Tick — one frame of engine work
//============================================================================

void Engine::Tick() {
    if (!running_) return;

    auto now = std::chrono::steady_clock::now();
    if (now - last_work_time_ < frame_interval_) return;

    FrameLoop();
    last_work_time_ = std::chrono::steady_clock::now();
}

//============================================================================
// Shutdown — request graceful stop
//============================================================================

void Engine::Shutdown() {
    if (running_) {
        running_ = false;
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
    if (cleaned_up_) return;
    cleaned_up_ = true;

    // Cancel frame timer before destroying Lua state.
    if (frame_timer_) {
        frame_timer_->Cancel();
        frame_timer_.reset();
    }

    if (script_vm_) {
        script_vm_->DestroyScript();
        int mem_kb = lua_gc(script_vm_->GetState(), LUA_GCCOUNT, 0);
        auto* logger = GetLogger();
        ENGINE_LOG_INFO(logger, "ScriptVM: final memory [{} KB], exiting", mem_kb);
    }

    script::ShutdownNetBindings();
    if (script_vm_) {
        script::ShutdownTimerBindings(*script_vm_);
    }
    TimerManager::destroy_instance();

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "timer manager shut down");

    // Release signal watchers.
    sigint_watcher_.reset();
#ifndef _WIN32
    sigterm_watcher_.reset();
#endif

    ShutdownLogger();
}

//============================================================================
// FrameLoop — per-frame work (timer update + Lua update)
//============================================================================

void Engine::FrameLoop() {
    if (!running_) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_frame_time_);
    last_frame_time_ = now;

    ++frame_count_;

    TimerManager::instance().update();

    if (script_vm_) {
        script_vm_->UpdateScript();
    }

    if (elapsed > frame_interval_ * 2) {
        if (frame_count_ - last_slow_frame_log_ > 30) {
            ENGINE_LOG_DEBUG(GetLogger(),
                             "frame [{}] took [{}ms] (slow)",
                             frame_count_, elapsed.count());
            last_slow_frame_log_ = frame_count_;
        }
    }
}

} // namespace engine
