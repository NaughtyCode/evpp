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
#include "runtime/engine/engine.h"
#if defined(ENGINE_MONGODB_ENABLED)
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_service_config.h"
#include "runtime/database/mongo/mongo_system.h"
#include "runtime/database/mongo/mongo_uri.h"
#endif
#include "runtime/physics/physics_engine_bridge.h"
#include "runtime/profiler/profiler_core.h"
#include "runtime/vm/coroutine_scheduler.h"
#include "runtime/vm/script_reloader.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/script_bind.h"
#include "runtime/space/connection_router.h"
#include "runtime/space/space_manager.h"
#include "runtime/space/space_message.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/vm.h"

#ifdef _WIN32
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
	switch (ctrl_type) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_LOGOFF_EVENT:
		engine::Engine::Instance().Shutdown();
		return TRUE;
	default:
		return FALSE;
	}
}
#endif

namespace engine {

namespace {
Engine* g_test_instance = nullptr;
}  // namespace

Engine& Engine::Instance() {
	if (g_test_instance) {
		return *g_test_instance;
	}
	static Engine instance;
	return instance;
}

void Engine::SetInstanceForTesting(Engine* test_instance) {
	g_test_instance = test_instance;
}

void Engine::ClearTestInstance() {
	g_test_instance = nullptr;
}

Engine::Engine() = default;
Engine::~Engine() {
	Cleanup();
}

ScriptVM& Engine::GetScriptVM() {
	if (!script_vm_) {
		std::fprintf(stderr,
					 "FATAL: GetScriptVM() called but ScriptVM is null. "
					 "Cleanup phase: %d. This is a lifecycle ordering bug.\n",
					 static_cast<int>(cleanup_phase_));
		std::exit(EXIT_FAILURE);
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

	// 鈹€鈹€ Profiler initialization 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	{
		ProfilerConfig prof_cfg;
		prof_cfg.buffer_size_kb = 32768;
		ProfilerManager::Get().Initialize(prof_cfg);
		ProfilerManager::Get().StartSession();
		ENGINE_LOG_INFO(logger,
						"profiler initialized and session started, "
						"enabled=[{}]",
						ProfilerManager::IsEnabled());
	}
	std::fprintf(stderr, "[engine] profiler initialized\n");

	ENGINE_PROFILE_SCOPE("engine", "Init");

	ENGINE_LOG_INFO(logger,
					"engine initializing, resource_dir=[{}], "
					"log_dir=[{}], log_level=[{}], "
					"runtime_scripts_dir=[{}], entry_scripts_dir=[{}], "
					"frame_interval=[{}ms], library_mode=[{}]",
					runtime_cfg.resource_dir,
					runtime_cfg.log.dir,
					runtime_cfg.log.level,
					runtime_cfg.scripts_dir,
					entry_scripts_dir,
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

	// 鈹€鈹€ MongoDB driver initialization 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
#if defined(ENGINE_MONGODB_ENABLED)
	{
		bool mongo_ok = mongo::MongoSystem::Instance().Initialize();
		ENGINE_LOG_INFO(logger, "mongo system initialized, ok=[{}]", mongo_ok);
	}

	// 鈹€鈹€ Database service initialization 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	{
		auto server_cfg = ConfigManager::Instance().GetServerConfig();
		DbServiceConfig db_svc_config;
		if (!server_cfg.db_service.empty()) {
			ConfigManager::LoadDbServiceConfigFromFile(server_cfg.db_service, db_svc_config);
		}

#ifndef NDEBUG
		auto& mongo_cfg = ConfigManager::Instance().GetMongoDbDevConfig();
#else
		auto& mongo_cfg = ConfigManager::Instance().GetMongoDbPublicConfig();
#endif
		auto uri = mongo::MongoUri::New(mongo_cfg.connection.uri.c_str());

		if (db_svc_config.connection_pool.wait_queue_timeout_ms > 0) {
			uri.SetOptionAsInt32(
				"waitQueueTimeoutMS",
				static_cast<int32_t>(db_svc_config.connection_pool.wait_queue_timeout_ms));
		}

		bool ok = DatabaseService::Instance().Initialize(db_svc_config, uri);
		ENGINE_LOG_INFO(logger, "database service initialized, ok=[{}]", ok);
	}
#endif

	if (runtime_cfg.frame.target_fps > 0) {
		frame_interval_ = std::chrono::milliseconds(1000 / runtime_cfg.frame.target_fps);
	} else {
		frame_interval_ = std::chrono::milliseconds(runtime_cfg.frame.interval_ms);
	}

	/* Map config string to sandbox level enum */
	LuaSandboxLevel sandbox_level = LuaSandboxLevel::Strict;
	if (runtime_cfg.sandbox_level == "server") {
			sandbox_level = LuaSandboxLevel::Server;
	} else if (runtime_cfg.sandbox_level == "full") {
			sandbox_level = LuaSandboxLevel::Full;
	}

	script_vm_ = std::make_unique<ScriptVM>(sandbox_level);
	ENGINE_LOG_INFO(logger, "lua vm initialized, version=[{}], sandbox=[{}]", ScriptVM::LuaVersion(), runtime_cfg.sandbox_level);

	// 鈹€鈹€ Physics system initialization 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	{
		std::fprintf(stderr, "[engine] initializing physics...\n");
		auto phys_cfg = runtime_cfg.resource_dir + "/physics/configs";
		auto phys_data = runtime_cfg.resource_dir + runtime_cfg.physics_scene_path;
		bool ok = PhysicsEngineBridge::Instance().Initialize(
			phys_cfg, phys_data, runtime_cfg.scripts_dir);
		if (!ok) {
			ENGINE_LOG_WARN(logger, "physics system failed to initialize");
		} else {
			fixed_delta_time_ = PhysicsEngineBridge::Instance().GetFixedDeltaTime();
			ENGINE_LOG_INFO(
				logger, "physics system initialized, fixed_delta_time=[{}s]", fixed_delta_time_);
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

	// Initialize coroutine scheduler (async.lua support)
	CoroutineScheduler::Instance().Init(script_vm_->GetState());

	// Initialize script hot-reload
	{
		std::string reload_root =
			std::filesystem::path(runtime_cfg.scripts_dir).parent_path().string();
		if (reload_root.empty()) reload_root = ".";
		script_reloader_ = std::make_unique<ScriptReloader>();
		script_reloader_->SetTarget(script_vm_.get(), {reload_root});
		script_reloader_->SetReloadCallback(
			[](const std::string& file, bool success) {
				auto* logger = GetLogger();
				if (success) {
					ENGINE_LOG_INFO(logger, "hot-reload OK: {}", file);
				} else {
					ENGINE_LOG_ERROR(logger, "hot-reload FAILED: {}", file);
				}
			});
	}

	last_frame_time_ = std::chrono::steady_clock::now();
	last_work_time_ = last_frame_time_;

	if (!entry_scripts_dir.empty()) {
		std::fprintf(stderr, "[engine] loading scripts from [%s]...\n", entry_scripts_dir.c_str());
		size_t failed = script_vm_->DoDirectory(entry_scripts_dir);
		if (failed > 0) {
			ENGINE_LOG_WARN(
				logger, "scripts dir [{}]: [{}] file(s) failed to load", entry_scripts_dir, failed);
		}
		script_vm_->InitScript();
		std::fprintf(stderr, "[engine] scripts loaded\n");
	}

	std::fprintf(stderr, "[engine] Init() complete\n");
}

//============================================================================
// Start 鈥?standalone mode: arm frame timer and signal watchers
//============================================================================

void Engine::Start() {
	std::fprintf(stderr, "[engine] Start() begin\n");
	ENGINE_PROFILE_SCOPE("engine", "Start");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "engine starting, frame_interval=[{}ms]", frame_interval_.count());

	// 鈹€鈹€ Start physics simulation 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	// Must be called after Initialize() and before the first Tick().
	// If Initialize() failed, Start() is a safe no-op.
	PhysicsEngineBridge::Instance().Start();

#ifdef _WIN32
	// Windows console control handler 鈥?graceful shutdown on Ctrl+C,
	// console close, system shutdown, or user logoff.
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	ENGINE_LOG_INFO(logger, "Windows console control handler installed");
#else
	sigint_watcher_ = std::make_unique<evpp::SignalEventWatcher>(SIGINT, loop_, [this]() {
		auto* logger = GetLogger();
		ENGINE_LOG_INFO(logger, "SIGINT received, shutting down...");
		Shutdown();
	});
	if (!sigint_watcher_->Init() || !sigint_watcher_->AsyncWait()) {
		ENGINE_LOG_ERROR(logger, "failed to initialize SIGINT watcher");
	}

	sigterm_watcher_ = std::make_unique<evpp::SignalEventWatcher>(SIGTERM, loop_, [this]() {
		auto* logger = GetLogger();
		ENGINE_LOG_INFO(logger, "SIGTERM received, shutting down...");
		Shutdown();
	});
	if (!sigterm_watcher_->Init() || !sigterm_watcher_->AsyncWait()) {
		ENGINE_LOG_ERROR(logger, "failed to initialize SIGTERM watcher");
	}
#endif

	frame_timer_ =
		loop_->RunEvery(evpp::Duration(frame_interval_.count() * evpp::Duration::kMillisecond),
						[this]() { Tick(); });

	running_ = true;
	last_frame_time_ = std::chrono::steady_clock::now();
	last_work_time_ = last_frame_time_;

	// Start hot-reload file watcher on the reloader's own thread.
	if (script_reloader_) {
		script_reloader_->Start();
	}


	std::fprintf(stderr, "[engine] Start() complete, running_=true\n");
}

//============================================================================
// Run 鈥?standalone convenience: Start + dispatch + Cleanup
//============================================================================

void Engine::Run() {
	std::fprintf(stderr, "[engine] Run() begin, calling Start()\n");
	Start();

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "entering main loop");
	std::fprintf(stderr, "[engine] entering main loop (loop_->Run())\n");
	loop_->Run();
	ENGINE_LOG_INFO(logger, "main loop exited, frame_count=[{}]", frame_count_.load());
	std::fprintf(stderr, "[engine] main loop exited\n");

	Cleanup();
}

//============================================================================
// Tick 鈥?one frame of engine work
//============================================================================

void Engine::Tick() {
	if (!running_) return;

	auto now = std::chrono::steady_clock::now();
	if (now - last_work_time_ < frame_interval_) return;

	{
		ENGINE_PROFILE_TICK();
		FrameLoop();
		last_work_time_ = std::chrono::steady_clock::now();
	}  // Tick slice ends
}

//============================================================================
// Shutdown 鈥?request graceful stop
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
// Cleanup 鈥?release all resources
//============================================================================

void Engine::Cleanup() {
	ENGINE_PROFILE_SCOPE("engine", "Cleanup");

	if (cleaned_up_.exchange(true)) return;

	cleanup_phase_ = CleanupPhase::PhysicsShutdown;
	PhysicsEngineBridge::Instance().Shutdown();

	cleanup_phase_ = CleanupPhase::DatabaseShutdown;
#if defined(ENGINE_MONGODB_ENABLED)
	DatabaseService::Instance().Shutdown();
	mongo::MongoSystem::Instance().Shutdown();
#endif

	if (frame_timer_) {
		frame_timer_->Cancel();
		frame_timer_.reset();
	}

	if (script_reloader_) {
		script_reloader_->Stop();
	}

	cleanup_phase_ = CleanupPhase::NetworkShutdown;
	assert(script_vm_ != nullptr);
	if (script_vm_) {
		script::ShutdownNetBindings();
	}

	cleanup_phase_ = CleanupPhase::TimerShutdown;
	assert(script_vm_ != nullptr);
	if (script_vm_) {
		script::ShutdownEntityBindings();
	}
	if (script_vm_) {
		script::ShutdownTimerBindings(*script_vm_);
	}

	cleanup_phase_ = CleanupPhase::ScriptDestroyed;
	if (script_vm_) {
		script_vm_->DestroyScript();
		int mem_kb = lua_gc(script_vm_->GetState(), LUA_GCCOUNT, 0);
		auto* logger = GetLogger();
		ENGINE_LOG_INFO(logger, "ScriptVM: final memory [{} KB], exiting", mem_kb);
	}
	script_vm_.reset();
	TimerManager::destroy_instance();

	cleanup_phase_ = CleanupPhase::FinalLogs;
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "timer manager shut down");

	sigint_watcher_.reset();
#ifndef _WIN32
	sigterm_watcher_.reset();
#endif

	{
		ENGINE_LOG_INFO(logger, "profiler: flushing, stopping, saving trace...");
		ProfilerManager::Get().Flush();
		ProfilerManager::Get().StopSession();
		ProfilerManager::Get().SaveTrace();
		ProfilerManager::Get().Shutdown();
	}

	cleanup_phase_ = CleanupPhase::Complete;
	ShutdownLogger();

	loop_ = nullptr;
}

//============================================================================
// FrameLoop 鈥?per-frame work (timer update + Lua update)
//============================================================================

void Engine::FrameLoop() {
	if (!running_) return;

	auto frame_start = std::chrono::steady_clock::now();
	auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - last_frame_time_);
	last_frame_time_ = frame_start;

	uint64_t fc = frame_count_.fetch_add(1, std::memory_order_relaxed) + 1;

	ENGINE_PROFILE_FRAME_BEGIN(fc, elapsed.count());

	{
		ENGINE_PROFILE_TIMER_UPDATE();
		TimerManager::instance().update();
	}  // TimerUpdate slice ends

	// [D17.1][D17.2] Trigger physics simulation (Tick enqueues command; physics thread steps)
	{
		ENGINE_PROFILE_PHYSICS_TICK(fc);
		PhysicsEngineBridge::Instance().Tick(fc, fixed_delta_time_);
	}  // PhysicsTick slice ends

	// [D17.3] Engine Lua script update + non-physics tasks
	{
		ENGINE_PROFILE_SCRIPT_UPDATE();
		if (script_vm_) {
			script_vm_->UpdateScript();
		}
	}  // ScriptUpdate slice ends

	// Resume runnable coroutines. Limit to 5ms per frame to avoid
	// starving the main loop when many coroutines are active.
	CoroutineScheduler::Instance().Update(5);

	// Update all active Spaces (per-space VM update).
	space::SpaceManager::Instance().ForEachSpace([&](space::Space& sp) {
		sp.Update(static_cast<int64_t>(elapsed.count()));
	});

	// Deliver pending cross-space messages.
	space::SpaceMessageRouter::Instance().ProcessPending();

	// [D17.4] Fetch physics result for this frame
	{
		ENGINE_PROFILE_PHYSICS_FETCH(fc);
		auto result = PhysicsEngineBridge::Instance().FetchResult(fc, 5);
		if (result && physics_result_handler_) {
			physics_result_handler_(*result);
		}
	}  // PhysicsFetch slice ends

	auto frame_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - frame_start)
							 .count();
	ENGINE_PROFILE_FRAME_END(frame_elapsed);

	// Slow frame detection
	ENGINE_PROFILE_SLOW_FRAME(frame_elapsed, frame_interval_.count() * 2);

	if (elapsed > frame_interval_ * 2) {
		if (fc - last_slow_frame_log_ > 30) {
			auto* logger = GetLogger();
			ENGINE_LOG_DEBUG(logger, "frame [{}] took [{}ms] (slow)", fc, elapsed.count());
			last_slow_frame_log_ = fc;
		}
	}
}

void Engine::SetPhysicsResultHandler(PhysicsResultHandler handler) {
	physics_result_handler_ = std::move(handler);
}

}  // namespace engine

