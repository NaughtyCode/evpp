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
#include <stdexcept>
#include <thread>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/event_watcher.h>
#include <runtime/evpp/invoke_timer.h>

#include "runtime/core/log/log.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/engine/engine.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/monitoring/metrics.h"
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_service_config.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_system.h"
#include "runtime/database/mongo/mongo_uri.h"
#endif
#include "runtime/physics/physics_engine_bridge.h"
#include "runtime/profiler/profiler_core.h"
#include "runtime/vm/coroutine_scheduler.h"
#include "runtime/vm/main_thread_vm.h"
#include "runtime/vm/script_reloader.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/bind/script_bind.h"
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
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_CRITICAL(logger,
				"GetScriptVM() called but ScriptVM is null. "
				"Cleanup phase: {}. This is a lifecycle ordering bug.",
				static_cast<int>(cleanup_phase_.load(std::memory_order_relaxed)));
		} else {
			std::fprintf(stderr,
				"FATAL: GetScriptVM() called but ScriptVM is null. "
				"Cleanup phase: %d. This is a lifecycle ordering bug.\n",
				static_cast<int>(cleanup_phase_.load(std::memory_order_relaxed)));
		}
		throw std::runtime_error(
			"GetScriptVM() called but ScriptVM is null — lifecycle ordering bug");
	}
	return *script_vm_;
}

// Init

void Engine::Init(const RuntimeConfig& runtime_cfg,
				  const std::string& entry_scripts_dir,
				  evpp::EventLoop* external_loop) {
	const bool library_mode = external_loop != nullptr;

	if (initialized_.load(std::memory_order_acquire) &&
		!cleaned_up_.load(std::memory_order_acquire)) {
		Cleanup();
	}
	cleaned_up_.store(false, std::memory_order_release);
	cleanup_phase_.store(CleanupPhase::NotStarted, std::memory_order_release);
	running_.store(false, std::memory_order_release);
	initialized_.store(false, std::memory_order_release);
	frame_count_.store(0, std::memory_order_release);
	hot_reload_start_scheduled_ = false;
	hot_reload_started_ = false;
	hot_reload_enable_time_ = std::chrono::steady_clock::time_point{};

	std::fprintf(stderr, "[engine] Init() begin\n");
	std::fprintf(stderr, "[engine] InitLogger...\n");
	InitLogger(runtime_cfg.log);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "logger created");
	monitoring::MetricsRegistry::Instance().RegisterBuiltinMetrics();

	// ---- Profiler initialization ----
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
	ENGINE_LOG_INFO(logger, "profiler initialized");

	ENGINE_PROFILE_SCOPE("engine", "Init");

	{
		auto sc = ConfigManager::Instance().GetServerConfig();
		ENGINE_LOG_INFO(logger,
					"engine initializing, instance=[{}], environment=[{}], resource_dir=[{}], "
					"log_dir=[{}], log_level=[{}], "
					"runtime_scripts_dir=[{}], entry_scripts_dir=[{}], "
					"frame_interval=[{}ms], hot_reload_enabled=[{}], "
					"hot_reload_startup_delay=[{}ms], library_mode=[{}]",
					sc.instance.id.empty() ? "(unset)" : sc.instance.id,
					runtime_cfg.environment,
					runtime_cfg.resource_dir,
					runtime_cfg.log.dir,
					runtime_cfg.log.level,
					runtime_cfg.scripts_dir,
					entry_scripts_dir,
					runtime_cfg.frame.interval_ms,
					runtime_cfg.hot_reload.enabled,
					runtime_cfg.hot_reload.startup_delay_ms,
					library_mode);
		}

	ENGINE_LOG_INFO(logger, "creating TimerManager...");
	timer_mgr_ = std::make_unique<TimerManager>();
	timer_mgr_->initialize();
	entity::EntityManager::Instance().SetTimerManager(timer_mgr_.get());
	ENGINE_LOG_INFO(logger, "timer manager initialized");

	if (external_loop) {
		loop_ = external_loop;
		running_ = true;  // library mode: engine is immediately "running"
	} else {
		ENGINE_LOG_INFO(logger, "creating EventLoop...");
		owned_loop_ = std::make_unique<evpp::EventLoop>();
		loop_ = owned_loop_.get();
		ENGINE_LOG_INFO(logger, "EventLoop created");
	}

	// ---- MongoDB driver initialization ----
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	{
		bool mongo_ok = mongo::MongoSystem::Instance().Initialize();
		ENGINE_LOG_INFO(logger, "mongo system initialized, ok=[{}]", mongo_ok);
	}

	// ---- Database service initialization ----
	{
		auto server_cfg = ConfigManager::Instance().GetServerConfig();
		DbServiceConfig db_svc_config;
		if (!server_cfg.db_service.empty()) {
			ConfigManager::LoadDbServiceConfigFromFile(server_cfg.db_service, db_svc_config);
		}

		// Runtime environment selects MongoDB cluster.
		// Explicit active_mongodb in server config takes priority.
		MongoDbConfig mongo_cfg;
		{
			auto server_cfg2 = ConfigManager::Instance().GetServerConfig();
			const auto& active = server_cfg2.active_mongodb;
			if (active == "public") {
				mongo_cfg = ConfigManager::Instance().GetMongoDbPublicConfig();
			} else if (active == "dev") {
				mongo_cfg = ConfigManager::Instance().GetMongoDbDevConfig();
			} else {
				Environment env = ParseEnvironment(runtime_cfg.environment);
				if (env == Environment::production) {
					mongo_cfg = ConfigManager::Instance().GetMongoDbPublicConfig();
				} else {
					mongo_cfg = ConfigManager::Instance().GetMongoDbDevConfig();
				}
			}
		}
		if (mongo_cfg.connection.uri.empty()) {
			ENGINE_LOG_INFO(logger, "database service skipped: no MongoDB URI configured");
			if (server_cfg.db_required) {
				throw std::runtime_error("database service required but no MongoDB URI configured");
			}
		} else {
			mongo::MongoError uri_error;
			auto uri = mongo::MongoUri::NewWithError(mongo_cfg.connection.uri.c_str(), &uri_error);
			if (!uri.RawUri()) {
				ENGINE_LOG_ERROR(logger,
								 "database service skipped: invalid MongoDB URI: {}",
								 uri_error.Message());
				if (server_cfg.db_required) {
					throw std::runtime_error("database service required but MongoDB URI is invalid");
				}
			} else {
				bool ok = DatabaseService::Instance().Initialize(db_svc_config, uri);
				ENGINE_LOG_INFO(logger, "database service initialized, ok=[{}]", ok);
				if (!ok && server_cfg.db_required) {
					throw std::runtime_error("database service required but initialization failed");
				}
			}
		}
	}
#endif

	if (runtime_cfg.frame.target_fps > 0) {
		frame_interval_ = std::chrono::milliseconds(1000 / runtime_cfg.frame.target_fps);
	} else {
		frame_interval_ = std::chrono::milliseconds(runtime_cfg.frame.interval_ms);
	}

	/* Map config SandboxLevel to LuaSandboxLevel */
	SandboxLevel config_sl = ParseSandboxLevel(runtime_cfg.sandbox_level);
	LuaSandboxLevel vm_sl;
	switch (config_sl) {
	case SandboxLevel::Full:   vm_sl = LuaSandboxLevel::Full;   break;
	case SandboxLevel::Server: vm_sl = LuaSandboxLevel::Server; break;
	default:                    vm_sl = LuaSandboxLevel::Strict; break;
	}

	script_vm_ = std::make_unique<MainThreadScriptVM>(vm_sl);
	ENGINE_LOG_INFO(logger, "lua vm initialized, version=[{}], sandbox=[{}]",
		ScriptVM::LuaVersion(), SandboxLevelToString(config_sl));

	// ---- Physics system initialization ----
	{
		ENGINE_LOG_INFO(logger, "initializing physics...");
		auto phys_cfg = runtime_cfg.resource_dir + "/physics/config";
		bool ok = PhysicsEngineBridge::Instance().Initialize(
			phys_cfg, runtime_cfg.scripts_dir);
		if (!ok) {
			ENGINE_LOG_WARN(logger, "physics system failed to initialize");
		} else {
			fixed_delta_time_ = PhysicsEngineBridge::Instance().GetFixedDeltaTime();
			ENGINE_LOG_INFO(
				logger, "physics system initialized, fixed_delta_time=[{}s]", fixed_delta_time_);
		}
		ENGINE_LOG_INFO(logger, "physics init done (ok={})", ok);
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
	script_vm_->ExportRuntimeBindings(*timer_mgr_);

	// Initialize coroutine scheduler (async.lua support)
	CoroutineScheduler::Instance().Init(script_vm_->GetState());

	// Initialize script hot-reload
	{
		std::string reload_root =
			std::filesystem::path(runtime_cfg.scripts_dir).parent_path().string();
		if (reload_root.empty()) reload_root = ".";
		script_reloader_ = std::make_unique<ScriptReloader>();
		script_reloader_->SetTarget(script_vm_.get(), {reload_root});
		script_reloader_->SetSandboxLevel(vm_sl);
		script_reloader_->SetEventLoop(loop_);
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
		ENGINE_LOG_INFO(logger, "loading scripts from [{}]...", entry_scripts_dir);
		size_t failed = script_vm_->DoDirectory(entry_scripts_dir);
		if (failed > 0) {
			ENGINE_LOG_WARN(
				logger, "scripts dir [{}]: [{}] file(s) failed to load", entry_scripts_dir, failed);
		}
		script_vm_->InitScript();
		ENGINE_LOG_INFO(logger, "scripts loaded");
	}

	// Start admin HTTP server (/health, /stats, /metrics) if configured.
	{
		auto server_cfg = ConfigManager::Instance().GetServerConfig();
		if (!library_mode && server_cfg.admin_port > 0 && loop_) {
			std::string bind_addr = server_cfg.admin_bind_address.empty()
										? "127.0.0.1" : server_cfg.admin_bind_address;
			if (admin_server_.Start(loop_, server_cfg.admin_port, bind_addr)) {
				ENGINE_LOG_INFO(logger, "admin HTTP server started on {}:{}", bind_addr, server_cfg.admin_port);
			}
		}
	}

	// Register config reload subscriber.
	// Callback stores changes for main-thread application in FrameLoop.
	{
		config_dir_ = std::filesystem::path(runtime_cfg.resource_dir).parent_path().string()
					  + "/config";
		if (config_reload_callback_id_ != 0) {
			ConfigManager::Instance().UnregisterReloadCallback(config_reload_callback_id_);
			config_reload_callback_id_ = 0;
		}
		config_reload_callback_id_ = ConfigManager::Instance().RegisterReloadCallback([this](const ConfigChangeSet& changes) {
			std::lock_guard<std::mutex> lock(pending_config_mutex_);
			// Merge changes — if the same field changed again before we
			// applied the previous batch, keep only the latest old→new.
			for (const auto& entry : changes) {
				bool found = false;
				for (auto& existing : pending_config_changes_) {
					if (existing.field_path == entry.field_path) {
						existing.new_value = entry.new_value;
						found = true;
						break;
					}
				}
				if (!found) {
					pending_config_changes_.push_back(entry);
				}
			}
			config_changes_pending_.store(true, std::memory_order_release);
		});
		ENGINE_LOG_INFO(logger, "config reload subscriber registered");
	}

	initialized_.store(true, std::memory_order_release);
	ENGINE_LOG_INFO(logger, "Init() complete");
	ScheduleScriptHotReloadStart(runtime_cfg);
}

void Engine::ScheduleScriptHotReloadStart(const RuntimeConfig& runtime_cfg) {
	auto* logger = GetLogger();

	hot_reload_enabled_ = runtime_cfg.hot_reload.enabled;
	hot_reload_startup_delay_ms_ = runtime_cfg.hot_reload.startup_delay_ms;
	hot_reload_poll_interval_ms_ = runtime_cfg.hot_reload.poll_interval_ms;
	hot_reload_debounce_ms_ = runtime_cfg.hot_reload.debounce_ms;

	if (hot_reload_startup_delay_ms_ < 0) hot_reload_startup_delay_ms_ = 0;
	if (hot_reload_poll_interval_ms_ < 1) hot_reload_poll_interval_ms_ = 1;
	if (hot_reload_debounce_ms_ < 0) hot_reload_debounce_ms_ = 0;

	hot_reload_started_ = false;
	hot_reload_start_scheduled_ = false;
	hot_reload_enable_time_ = std::chrono::steady_clock::time_point{};

	if (!script_reloader_) {
		ENGINE_LOG_WARN(logger, "ScriptReloader: unavailable, hot-reload remains idle");
		return;
	}
	if (!hot_reload_enabled_) {
		ENGINE_LOG_INFO(logger, "ScriptReloader: disabled by config, file watching remains idle");
		return;
	}
#if !ENGINE_FILE_WATCHER_ENABLED
	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: file watching disabled on [{}], "
	                "hot-reload remains idle",
	                ENGINE_PLATFORM_NAME);
	return;
#endif

	hot_reload_enable_time_ =
		std::chrono::steady_clock::now() +
		std::chrono::milliseconds(hot_reload_startup_delay_ms_);
	hot_reload_start_scheduled_ = true;

	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: idle after successful runtime startup; "
	                "file watching will start in [{}ms] "
	                "(poll=[{}ms], debounce=[{}ms])",
	                hot_reload_startup_delay_ms_,
	                hot_reload_poll_interval_ms_,
	                hot_reload_debounce_ms_);
}

void Engine::MaybeStartScriptHotReload() {
	if (!hot_reload_start_scheduled_ || hot_reload_started_ || !script_reloader_) return;
	if (std::chrono::steady_clock::now() < hot_reload_enable_time_) return;

	hot_reload_start_scheduled_ = false;
	auto* logger = GetLogger();
	try {
		script_reloader_->Start(hot_reload_poll_interval_ms_, hot_reload_debounce_ms_);
		hot_reload_started_ = true;
		ENGINE_LOG_INFO(logger,
		                "ScriptReloader: hot-reload file watching active after startup delay "
		                "[{}ms]",
		                hot_reload_startup_delay_ms_);
	} catch (const std::exception& e) {
		ENGINE_LOG_ERROR(logger, "ScriptReloader: failed to start file watching: {}", e.what());
	} catch (...) {
		ENGINE_LOG_ERROR(logger, "ScriptReloader: failed to start file watching: unknown error");
	}
}

// Start -- standalone mode: arm frame timer and signal watchers

void Engine::Start() {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Start() begin");
	ENGINE_PROFILE_SCOPE("engine", "Start");

	ENGINE_LOG_INFO(logger, "engine starting, frame_interval=[{}ms]", frame_interval_.count());

	// ---- Start physics simulation ----
	// Must be called after Initialize() and before the first Tick().
	// If Initialize() failed, Start() is a safe no-op.
	PhysicsEngineBridge::Instance().Start();

#ifdef _WIN32
	// Windows console control handler -- graceful shutdown on Ctrl+C,
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

	sighup_watcher_ = std::make_unique<evpp::SignalEventWatcher>(SIGHUP, loop_, [this]() {
		auto* logger = GetLogger();
		ENGINE_LOG_INFO(logger, "SIGHUP received, reloading config...");
		if (!config_dir_.empty()) {
			bool ok = ConfigManager::Instance().Reload(config_dir_);
			ENGINE_LOG_INFO(logger, "SIGHUP config reload: {}", ok ? "OK" : "FAILED");
		}
	});
	if (!sighup_watcher_->Init() || !sighup_watcher_->AsyncWait()) {
		ENGINE_LOG_ERROR(logger, "failed to initialize SIGHUP watcher");
	}
#endif

	frame_timer_ =
		loop_->RunEvery(evpp::Duration(static_cast<int64_t>(frame_interval_.count()) *
									   evpp::Duration::kMillisecond),
						[this]() { Tick(); });

	running_ = true;
	last_frame_time_ = std::chrono::steady_clock::now();
	last_work_time_ = last_frame_time_;



	ENGINE_LOG_INFO(logger, "Start() complete, running_=true");
}

// Run -- standalone convenience: Start + dispatch + Cleanup

void Engine::Run() {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Run() begin, calling Start()");
	Start();


	ENGINE_LOG_INFO(logger, "entering main loop (loop_->Run())");
	loop_->Run();
	ENGINE_LOG_INFO(logger, "main loop exited, frame_count=[{}]", frame_count_.load());
	ENGINE_LOG_INFO(logger, "main loop exited");

	Cleanup();
}

// Tick -- one frame of engine work

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

// Shutdown -- request graceful stop

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

// ApplyConfigChanges — apply pending config changes on the main thread.

void Engine::ApplyConfigChanges() {
	ConfigChangeSet changes;
	{
		std::lock_guard<std::mutex> lock(pending_config_mutex_);
		changes = std::move(pending_config_changes_);
		pending_config_changes_.clear();
	}

	if (changes.empty()) return;

	auto* logger = GetLogger();
	for (const auto& entry : changes) {
		if (entry.field_path == "frame.target_fps" ||
			entry.field_path == "frame.interval_ms") {
			auto rt = ConfigManager::Instance().GetRuntimeConfig();
			std::chrono::milliseconds new_interval;
			if (rt.frame.target_fps > 0) {
				new_interval = std::chrono::milliseconds(1000 / rt.frame.target_fps);
			} else {
				new_interval = std::chrono::milliseconds(rt.frame.interval_ms);
			}

			if (new_interval != frame_interval_) {
				frame_interval_ = new_interval;
				// Reschedule frame timer on this (main) thread.
				if (frame_timer_ && running_ && loop_) {
					frame_timer_->Cancel();
					frame_timer_ = loop_->RunEvery(
						evpp::Duration(static_cast<int64_t>(frame_interval_.count()) *
									   evpp::Duration::kMillisecond),
						[this]() { Tick(); });
				}
				ENGINE_LOG_INFO(logger,
					"engine: frame_interval updated to {}ms (target_fps={})",
					frame_interval_.count(), rt.frame.target_fps);
			}
		} else if (entry.field_path == "log.level") {
			auto rt = ConfigManager::Instance().GetRuntimeConfig();
			ENGINE_LOG_INFO(logger,
				"engine: log level changed to {} — restart required for full effect",
				rt.log.level);
		} else if (entry.field_path == "log.dir") {
			auto rt = ConfigManager::Instance().GetRuntimeConfig();
			if (logger) {
				ENGINE_LOG_INFO(logger,
					"engine: log dir changed to {} — restart required for full effect",
					rt.log.dir);
			}
		} else if (entry.field_path == "sandbox_level") {
			ENGINE_LOG_WARN(logger,
				"engine: sandbox_level changed to {} — restart required for VM sandbox change",
				entry.new_value);
		} else if (entry.field_path.rfind("hot_reload.", 0) == 0) {
			ENGINE_LOG_WARN(logger,
				"engine: {} changed to {} — restart required for hot-reload scheduler change",
				entry.field_path, entry.new_value);
		}
		// Other fields (resource_dir, server settings)
		// are logged by ConfigManager::Reload() — consumers read them on demand.
	}
}

// Cleanup -- release all resources

void Engine::Cleanup() {
	if (!initialized_.load(std::memory_order_acquire) &&
		!running_.load(std::memory_order_acquire) &&
		!loop_ && !owned_loop_ && !timer_mgr_ && !script_vm_ &&
		!script_reloader_ && !frame_timer_) {
		cleaned_up_.store(true, std::memory_order_release);
		initialized_.store(false, std::memory_order_release);
		running_.store(false, std::memory_order_release);
		cleanup_phase_.store(CleanupPhase::Complete, std::memory_order_release);
		return;
	}
	if (cleaned_up_.exchange(true)) return;
	running_.store(false, std::memory_order_release);

	auto cleanup_start = std::chrono::steady_clock::now();
	auto server_cfg = ConfigManager::Instance().GetServerConfig();
	int shutdown_timeout = server_cfg.shutdown_timeout_sec;
	int drain_timeout = server_cfg.connection_drain_timeout_sec;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
	                "Cleanup: starting, shutdown_timeout={}s, drain_timeout={}s",
	                shutdown_timeout, drain_timeout);

	if (config_reload_callback_id_ != 0) {
		ConfigManager::Instance().UnregisterReloadCallback(config_reload_callback_id_);
		config_reload_callback_id_ = 0;
	}

	bool cleanup_timed_out = false;
	auto check_timeout = [&](const char* phase_name) -> bool {
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - cleanup_start).count();
		if (shutdown_timeout > 0 && elapsed > shutdown_timeout) {
			auto* log = GetLogger();
			ENGINE_LOG_CRITICAL(log,
			                    "Cleanup: timeout after {}s at phase {}, forcing best-effort teardown",
			                    elapsed, phase_name);
			cleanup_timed_out = true;
			return false;
		}
		return true;
	};

	cleanup_phase_.store(CleanupPhase::PhysicsShutdown, std::memory_order_release);
	PhysicsEngineBridge::Instance().Shutdown();
	// Stop hot-reload before any VM teardown to prevent watcher thread from accessing Lua state.
	hot_reload_start_scheduled_ = false;
	hot_reload_started_ = false;
	hot_reload_enable_time_ = std::chrono::steady_clock::time_point{};
	if (script_reloader_) {
		script_reloader_->Stop();
		script_reloader_.reset();
	}
	check_timeout("PhysicsShutdown");

	cleanup_phase_.store(CleanupPhase::DatabaseShutdown, std::memory_order_release);
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	DatabaseService::Instance().Shutdown();
	mongo::MongoSystem::Instance().Shutdown();
#endif
	check_timeout("DatabaseShutdown");

	if (frame_timer_) {
		frame_timer_->Cancel();
		frame_timer_.reset();
	}

	admin_server_.Stop();

	// ── Connection draining ──────────────────────────────────────────
	// The event loop has already stopped, so no new connections are being
	// accepted.  Give in-flight work a grace window to complete before we
	// tear down the network layer.
	{
		const int64_t active_connections =
			monitoring::MetricsRegistry::Instance().connections_active().Value();
		if (!cleanup_timed_out && drain_timeout > 0 && active_connections > 0) {
			auto drain_deadline = std::chrono::steady_clock::now()
				+ std::chrono::seconds(drain_timeout);
			ENGINE_LOG_INFO(logger,
							"Cleanup: draining [{}] active connection(s) ({}s timeout)...",
							active_connections, drain_timeout);

			while (std::chrono::steady_clock::now() < drain_deadline) {
				if (monitoring::MetricsRegistry::Instance().connections_active().Value() <= 0) {
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		} else {
			ENGINE_LOG_INFO(logger, "Cleanup: no active connections to drain");
		}
		ENGINE_LOG_INFO(logger, "Cleanup: drain phase complete, proceeding to network shutdown");
	}

	cleanup_phase_.store(CleanupPhase::NetworkShutdown, std::memory_order_release);
	if (script_vm_) {
		script_vm_->ShutdownNetworkBindings();
	}
	check_timeout("NetworkShutdown");

	cleanup_phase_.store(CleanupPhase::TimerShutdown, std::memory_order_release);
	if (script_vm_) {
		script_vm_->ShutdownTimerBindings();
	}
	check_timeout("TimerShutdown");

	cleanup_phase_.store(CleanupPhase::ScriptDestroyed, std::memory_order_release);
	if (script_vm_) {
		script_vm_->DestroyScript();
		script_vm_->ShutdownProfilerBindings();
		int mem_kb = lua_gc(script_vm_->GetState(), LUA_GCCOUNT, 0);
		ENGINE_LOG_INFO(logger, "ScriptVM: final memory [{} KB], exiting", mem_kb);
	}
	CoroutineScheduler::Instance().CancelAll();
	script_vm_.reset();

	if (timer_mgr_) {
		entity::EntityManager::Instance().DestroyAll();
		entity::EntityManager::Instance().SetTimerManager(nullptr);
		timer_mgr_->shutdown();
		timer_mgr_.reset();
	}

	cleanup_phase_.store(CleanupPhase::FinalLogs, std::memory_order_release);
	ENGINE_LOG_INFO(logger, "timer manager shut down");

	sigint_watcher_.reset();
#ifndef _WIN32
	sigterm_watcher_.reset();
	sighup_watcher_.reset();
#endif

	{
		ENGINE_LOG_INFO(logger, "profiler: flushing, stopping, saving trace...");
		ProfilerManager::Get().Flush();
		ProfilerManager::Get().StopSession();
		ProfilerManager::Get().SaveTrace();
		ProfilerManager::Get().Shutdown();
	}

	auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - cleanup_start).count();
	if (cleanup_timed_out) {
		ENGINE_LOG_CRITICAL(logger, "Cleanup: best-effort teardown complete after timeout, total_time={}s",
							total_elapsed);
	} else {
		ENGINE_LOG_INFO(logger, "Cleanup: complete, total_time={}s", total_elapsed);
	}

	cleanup_phase_.store(CleanupPhase::Complete, std::memory_order_release);
	initialized_.store(false, std::memory_order_release);
	ShutdownLogger();

	loop_ = nullptr;
}

// FrameLoop -- per-frame work (timer update + Lua update)

void Engine::FrameLoop() {
	if (!running_) return;

	// Apply any pending config changes from a hot-reload.
	if (config_changes_pending_.load(std::memory_order_acquire)) {
		ApplyConfigChanges();
		config_changes_pending_.store(false, std::memory_order_release);
	}
	MaybeStartScriptHotReload();

	auto frame_start = std::chrono::steady_clock::now();
	auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - last_frame_time_);
	last_frame_time_ = frame_start;

	uint64_t fc = frame_count_.fetch_add(1, std::memory_order_relaxed) + 1;

	ENGINE_PROFILE_FRAME_BEGIN(fc, elapsed.count());

	{
		ENGINE_PROFILE_TIMER_UPDATE();
		timer_mgr_->update();
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
			// Deliver configuration change callbacks bound via config.on_change().
			script::FlushConfigCallbacks(script_vm_->GetState());
			script_vm_->UpdateScript();
			script::UpdateRpcBindings(*script_vm_);
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
