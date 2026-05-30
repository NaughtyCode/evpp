#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_thread.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/physics/physics_log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {

// InitTimerManager — create per-thread TimerManager (MT, before Start)

void PhysicsThread::InitTimerManager() {
	timer_mgr_ = std::make_unique<TimerManager>();
}

// Destructor

PhysicsThread::~PhysicsThread() {
	Stop();
	timer_mgr_.reset();
}

// CreatePhysicsLogger - map PhysicsLogConfig to engine::LogConfig [D7][D8]

quill::Logger* PhysicsThread::CreatePhysicsLogger(const PhysicsLogConfig& log_config) {
	engine::LogConfig mapped;
	mapped.dir = log_config.log_dir;
	mapped.log_filename = log_config.file_name;
	mapped.level = log_config.level;
	mapped.rotation_size_mb = log_config.max_file_size_mb;
	mapped.max_backup_files = log_config.max_backup_files;
	mapped.rotation_frequency = log_config.rotation_frequency;
	mapped.rotation_interval = log_config.rotation_interval;
	mapped.rotation_time_daily = log_config.rotation_time_daily;
	mapped.format_pattern = log_config.format_pattern;

	// Use the physics-specific logger name
	mapped.logger_name = "physics";

	return engine::CreateLogger(mapped);
}

// Start

bool PhysicsThread::Start(const PhysicsConfig& config,
						  const ThreadingConfig& threading,
						  const ThresholdsConfig& thresholds,
						  const PhysicsLogConfig& log_config,
						  const std::string& assets_path,
						  const std::string& restore_state) {
	if (running_.load(std::memory_order_acquire)) {
		return false;
	}
	if (thread_ && thread_->joinable()) {
		Stop();
	}

	physics_config_ = config;
	threading_config_ = threading;
	thresholds_config_ = thresholds;
	log_config_ = log_config;
	assets_path_ = assets_path;
	restore_state_on_start_ = restore_state;
	healthy_.store(false, std::memory_order_release);

	// Create independent logger
	logger_ = CreatePhysicsLogger(log_config);
	if (!logger_) {
		std::fprintf(stderr, "PhysicsThread: failed to create logger\n");
		return false;
	}

	PHYSICS_LOG_INFO(logger_,
					 "PhysicsThread: starting, thread_priority=[{}], "
					 "affinity=[{}], assets=[{}]",
					 threading.thread_priority,
					 threading.affinity_mask,
					 assets_path);

	// Start the physics thread
	running_.store(true, std::memory_order_release);
	try {
		thread_ = std::make_unique<std::thread>([this]() { EventLoop(); });
	} catch (const std::exception& e) {
		running_.store(false, std::memory_order_release);
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: failed to create thread: {}", e.what());
		return false;
	}

	// Set thread priority
	if (threading.thread_priority == "high") {
		// Windows: THREAD_PRIORITY_HIGHEST / Linux: SCHED_FIFO
#ifdef _WIN32
		SetThreadPriority(thread_->native_handle(), THREAD_PRIORITY_HIGHEST);
#else
		// POSIX sched_setscheduler would go here
#endif
	} else if (threading.thread_priority == "low") {
#ifdef _WIN32
		SetThreadPriority(thread_->native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	}

	// Set CPU affinity if specified
	if (threading.affinity_mask != 0) {
#ifdef _WIN32
		SetThreadAffinityMask(thread_->native_handle(),
							  static_cast<DWORD_PTR>(threading.affinity_mask));
#else
		// pthread_setaffinity_np would go here
#endif
	}

	{
		std::unique_lock<std::mutex> lock(health_cv_mutex_);
		bool startup_finished = health_cv_.wait_for(
			lock, std::chrono::milliseconds(5000), [this]() {
				return healthy_.load(std::memory_order_acquire) ||
					   !running_.load(std::memory_order_acquire);
			});
		if (!startup_finished || !healthy_.load(std::memory_order_acquire)) {
			PHYSICS_LOG_ERROR(logger_,
							  "PhysicsThread: startup failed or timed out "
							  "(healthy=[{}], running=[{}])",
							  healthy_.load(std::memory_order_acquire),
							  running_.load(std::memory_order_acquire));
			Stop();
			return false;
		}
	}

	return true;
}

// Stop

void PhysicsThread::Stop() {
	if (!running_.load(std::memory_order_acquire) && !thread_) {
		return;
	}

	PHYSICS_LOG_INFO(logger_, "PhysicsThread: stopping...");

	// Signal thread to exit
	running_.store(false, std::memory_order_release);
	cv_.notify_one();  // wake EventLoop from wait (no fake tick needed)
	result_cv_.notify_all();
	health_cv_.notify_all();

	// Join thread
	if (thread_ && thread_->joinable()) {
		if (thread_->get_id() == std::this_thread::get_id()) {
			PHYSICS_LOG_ERROR(logger_, "PhysicsThread: Stop called from physics thread");
			return;
		}
		thread_->join();
	}
	thread_.reset();
	physics_thread_id_ = std::thread::id{};
	healthy_.store(false, std::memory_order_release);

	PHYSICS_LOG_INFO(logger_, "PhysicsThread: stopped");
}

// Recover - restart physics thread after a crash [D21]

bool PhysicsThread::Recover(const std::string& saved_state) {
	if (IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: recovery cannot run on the physics thread");
		return false;
	}

	PHYSICS_LOG_WARN(logger_, "PhysicsThread: attempting recovery...");
	PHYSICS_LOG_WARN(
		logger_, "PhysicsThread: was healthy=[{}], running=[{}]", healthy_.load(), running_.load());

	// Stop the old thread (Stop joins, so the thread is fully done here).
	Stop();

	// Restart with the same config
	bool ok = Start(physics_config_,
					threading_config_,
					thresholds_config_,
					log_config_,
					assets_path_,
					saved_state);
	if (!ok) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: recovery failed Start() returned false");
		return false;
	}

	PHYSICS_LOG_INFO(logger_, "PhysicsThread: recovery complete");
	return true;
}

std::string PhysicsThread::SaveState() const {
	if (running_.load(std::memory_order_acquire) && !IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: SaveState must run on the physics thread");
		assert(false && "PhysicsThread::SaveState called from the wrong thread");
		return {};
	}
	return world_.SaveState();
}

bool PhysicsThread::RestoreState(const std::string& data) {
	if (running_.load(std::memory_order_acquire) && !IsPhysicsThread()) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: RestoreState must run on the physics thread");
		assert(false && "PhysicsThread::RestoreState called from the wrong thread");
		return false;
	}
	return world_.RestoreState(data);
}

// EnqueueCommand

bool PhysicsThread::EnqueueCommand(PhysicsCommand cmd) {
	if (!running_.load(std::memory_order_acquire)) {
		return false;
	}

	// Queue backpressure: commandQueueSize controls command capacity.
	// Dropping non-Tick commands because maxPendingFrames is low can lose
	// gameplay-critical operations such as Destroy or SetVelocity.
	size_t approx_size = command_queue_.size_approx();
	if (static_cast<int>(approx_size) >= threading_config_.command_queue_size) {
		PHYSICS_LOG_WARN(logger_,
						 "PhysicsThread: command queue full "
						 "(approx=[{}], max=[{}]), dropping command",
						 approx_size,
						 threading_config_.command_queue_size);
		return false;
	}

	bool enqueued = command_queue_.enqueue(std::move(cmd));
	cv_.notify_one();  // wake EventLoop from wait
	return enqueued;
}

// TryDequeueResult

std::unique_ptr<PhysicsFrameResult> PhysicsThread::TryDequeueResult() {
	PhysicsFrameResult result;
	if (result_queue_.try_dequeue(result)) {
		return std::make_unique<PhysicsFrameResult>(std::move(result));
	}
	return nullptr;
}

void PhysicsThread::NotifyResult() {
	result_cv_.notify_one();
}

void PhysicsThread::WaitForResult(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> lock(result_cv_mutex_);
	result_cv_.wait_for(lock, timeout);
}

// VerifyIsPhysicsThread - runtime guard for PT-only code

void PhysicsThread::VerifyIsPhysicsThread() const {
	// Skip check if physics_thread_id_ hasn't been captured yet
	// (default-constructed thread::id means "not a thread").
	if (physics_thread_id_ != std::thread::id{}) {
		assert(physics_thread_id_ == std::this_thread::get_id() &&
			   "PhysicsThread: PT-only code called from wrong thread. "
			   "This code must only execute on the dedicated physics thread.");
	}
}

// EventLoop - runs on the dedicated physics thread

void PhysicsThread::EventLoop() {
	// Capture the physics thread ID once, at the start of the event loop.
	// Used by VerifyIsPhysicsThread() to assert that PT-only code (e.g.
	// PhysicsSystem::UpdateScript) is actually executing on this thread.
	physics_thread_id_ = std::this_thread::get_id();

	PHYSICS_LOG_INFO(logger_, "PhysicsThread: event loop started");

	if (timer_mgr_) {
		timer_mgr_->initialize();
	}

	auto shutdown_thread_resources = [this]() {
		if (shutdown_callback_) {
			shutdown_callback_();
		}
		if (timer_mgr_) {
			timer_mgr_->shutdown();
		}
	};

	// Initialize PhysicsWorld with configs captured at Start()
	bool ok = world_.Initialize(
		physics_config_, threading_config_, thresholds_config_, logger_, assets_path_);
	if (!ok) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: world initialization failed");
		healthy_.store(false, std::memory_order_release);
		running_.store(false, std::memory_order_release);
		shutdown_thread_resources();
		health_cv_.notify_all();
		return;
	}
	if (!restore_state_on_start_.empty()) {
		if (!world_.RestoreState(restore_state_on_start_)) {
			PHYSICS_LOG_ERROR(logger_,
							  "PhysicsThread: state restoration failed during startup "
							  "([{}] bytes)",
							  restore_state_on_start_.size());
			restore_state_on_start_.clear();
			healthy_.store(false, std::memory_order_release);
			running_.store(false, std::memory_order_release);
			shutdown_thread_resources();
			health_cv_.notify_all();
			return;
		}
		PHYSICS_LOG_INFO(logger_,
						 "PhysicsThread: state restored during startup ([{}] bytes)",
						 restore_state_on_start_.size());
		restore_state_on_start_.clear();
	}
	if (startup_callback_ && !startup_callback_()) {
		PHYSICS_LOG_ERROR(logger_, "PhysicsThread: startup callback failed");
		healthy_.store(false, std::memory_order_release);
		running_.store(false, std::memory_order_release);
		shutdown_thread_resources();
		health_cv_.notify_all();
		return;
	}
	healthy_.store(true, std::memory_order_release);
	health_cv_.notify_all();  // wake Start()/Recover() waiter
	PHYSICS_LOG_INFO(logger_, "PhysicsThread: world initialized, entering event loop");

	// ── Main event loop ──────────────────────────────────────────────
	while (running_.load(std::memory_order_acquire)) {
		PhysicsCommand cmd;
		bool got = command_queue_.try_dequeue(cmd);

		if (!got) {
			// Wait with timeout as safety net for missed signals.
			std::unique_lock<std::mutex> lock(cv_mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
				return !running_.load(std::memory_order_acquire) ||
				       command_queue_.size_approx() > 0;
			});
			continue;
		}

		{
			ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE(cmd);
			try {
				switch (cmd.type) {
				case CommandType::Spawn: {
					auto& args = std::get<SpawnArgs>(cmd.args);
					world_.CreateBody(args.proto_id, args.position, args.rotation, args.user_data);
					break;
				}
				case CommandType::Destroy: {
					auto& args = std::get<DestroyArgs>(cmd.args);
					world_.DestroyBody(args.body_id);
					break;
				}
				case CommandType::ApplyForce: {
					auto& args = std::get<ApplyForceArgs>(cmd.args);
					world_.ApplyForce(args.body_id, args.force, args.point);
					break;
				}
				case CommandType::SetVelocity: {
					auto& args = std::get<SetVelocityArgs>(cmd.args);
					world_.SetVelocity(args.body_id, args.velocity);
					break;
				}
				case CommandType::Tick: {
					auto& args = std::get<TickArgs>(cmd.args);
					// delta=0 is wakeup sentinel - skip simulation
					if (args.delta_time > 0.0f) {
						PhysicsFrameResult result = world_.Step(args.delta_time, args.frame_id);

						// Drive Lua collision callbacks on the physics thread
						// before enqueuing the result for the main thread.
						if (post_step_callback_) {
							post_step_callback_(result.collision_events);
						}

						// Update per-thread timer subsystem
						if (timer_mgr_) {
							timer_mgr_->update();
						}

						{
							ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE();
							result_queue_.enqueue(std::move(result));
						}  // ResultEnqueue slice ends

						// Result pile-up protection [D23]: drop oldest if over limit.
						while (result_queue_.size_approx() >
							   static_cast<size_t>(threading_config_.result_queue_size)) {
							PhysicsFrameResult dropped;
							result_queue_.try_dequeue(dropped);
							PHYSICS_LOG_WARN(logger_,
											 "PhysicsThread: frame pile-up, dropped frame [{}]",
											 dropped.frame_id);
						}
						result_cv_.notify_one();
					}
					break;
				}
				default:
					break;
				}
			} catch (const std::exception& e) {
				PHYSICS_LOG_ERROR(logger_, "PhysicsThread: exception in event loop: {}", e.what());
				healthy_.store(false, std::memory_order_release);
				running_.store(false, std::memory_order_release);
				health_cv_.notify_all();
				result_cv_.notify_all();
				break;	// exit event loop - world may be in corrupted state
			} catch (...) {
				PHYSICS_LOG_ERROR(logger_, "PhysicsThread: unknown exception in event loop");
				healthy_.store(false, std::memory_order_release);
				running_.store(false, std::memory_order_release);
				health_cv_.notify_all();
				result_cv_.notify_all();
				break;
			}
		}  // CmdDequeue slice ends
	}

	PHYSICS_LOG_INFO(logger_, "PhysicsThread: event loop exited");
	shutdown_thread_resources();
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
