#include "engine/engine/engine.h"

#include <csignal>
#include <memory>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include <evpp/event_loop.h>
#include <evpp/event_watcher.h>
#include <evpp/invoke_timer.h>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/core/timer/timer_manager.h"
#include "engine/vm/vm.h"

namespace engine {

Engine& Engine::Instance() {
    static Engine instance;
    return instance;
}

Engine::Engine() = default;
Engine::~Engine() = default;

ScriptVM& Engine::GetScriptVM() {
    return *script_vm_;
}

void Engine::Init(const std::string& log_dir, const std::string& scripts_dir) {
    InitLogger(log_dir);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "engine initializing, log_dir=[{}]", log_dir);

    TimerManager::create_instance();
    ENGINE_LOG_INFO(logger, "timer manager initialized");

    loop_ = std::make_unique<evpp::EventLoop>();

    script_vm_ = std::make_unique<ScriptVM>();
    ENGINE_LOG_INFO(logger, "lua vm initialized, version=[{}]", ScriptVM::LuaVersion());

    if (!scripts_dir.empty()) {
        size_t failed = script_vm_->DoDirectory(scripts_dir);
        if (failed > 0) {
            ENGINE_LOG_WARN(logger, "scripts dir [{}]: [{}] file(s) failed to load",
                            scripts_dir, failed);
        }
        script_vm_->InitScript();
    }
}

void Engine::Run() {
    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "engine starting, frame_interval=[{}ms]",
                    frame_interval_.count());

    auto sigint_watcher = std::make_unique<evpp::SignalEventWatcher>(
        SIGINT, loop_.get(), [this]() {
            ENGINE_LOG_INFO(GetLogger(), "SIGINT received, shutting down...");
            Shutdown();
        });
    sigint_watcher->Init();
    sigint_watcher->AsyncWait();

#ifndef _WIN32
    auto sigterm_watcher = std::make_unique<evpp::SignalEventWatcher>(
        SIGTERM, loop_.get(), [this]() {
            ENGINE_LOG_INFO(GetLogger(), "SIGTERM received, shutting down...");
            Shutdown();
        });
    sigterm_watcher->Init();
    sigterm_watcher->AsyncWait();
#endif

    auto frame_timer = loop_->RunEvery(
        evpp::Duration(frame_interval_.count() * evpp::Duration::kMillisecond),
        [this]() { FrameLoop(); });

    running_ = true;
    last_frame_time_ = std::chrono::steady_clock::now();

    ENGINE_LOG_INFO(logger, "entering main loop");
    loop_->Run();
    ENGINE_LOG_INFO(logger, "main loop exited, frame_count=[{}]", frame_count_);

    if (script_vm_) {
        script_vm_->DestroyScript();
    }

    TimerManager::destroy_instance();
    ENGINE_LOG_INFO(logger, "timer manager shut down");

    frame_timer->Cancel();
    sigint_watcher.reset();
#ifndef _WIN32
    sigterm_watcher.reset();
#endif
}

void Engine::Shutdown() {
    if (running_) {
        running_ = false;
        loop_->Stop();
    }
}

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
