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

//============================================================================
// Destructor
//============================================================================

PhysicsThread::~PhysicsThread() {
    Stop();
}

//============================================================================
// CreatePhysicsLogger — map PhysicsLogConfig to engine::LogConfig [D7][D8]
//============================================================================

quill::Logger* PhysicsThread::CreatePhysicsLogger(
    const PhysicsLogConfig& log_config) {
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

//============================================================================
// Start
//============================================================================

bool PhysicsThread::Start(const PhysicsConfig& config,
                           const ThreadingConfig& threading,
                           const ThresholdsConfig& thresholds,
                           const PhysicsLogConfig& log_config,
                           const std::string& assets_path) {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }

    physics_config_ = config;
    threading_config_ = threading;
    thresholds_config_ = thresholds;
    log_config_ = log_config;
    assets_path_ = assets_path;

    // Create independent logger
    logger_ = CreatePhysicsLogger(log_config);
    if (!logger_) {
        std::fprintf(stderr, "PhysicsThread: failed to create logger\n");
        return false;
    }

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: starting, thread_priority=[{}], "
                    "affinity=[{}], assets=[{}]",
                    threading.thread_priority, threading.affinity_mask, assets_path);

    // Start the physics thread
    running_.store(true, std::memory_order_release);
    thread_ = std::make_unique<std::thread>([this]() {
        EventLoop();
    });

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

    return true;
}

//============================================================================
// Stop
//============================================================================

void PhysicsThread::Stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: stopping...");

    // Signal thread to exit
    running_.store(false, std::memory_order_release);

    // Send wakeup tick to unblock the event loop
    command_queue_.enqueue(PhysicsCommand::MakeTick(TickArgs{0, 0.0f}));

    // Join thread
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: stopped");
}

//============================================================================
// Recover — restart physics thread after a crash [D21]
//============================================================================

bool PhysicsThread::Recover(const std::string& saved_state) {
    PHYSICS_LOG_WARN(logger_,"PhysicsThread: attempting recovery...");
    PHYSICS_LOG_WARN(logger_,"PhysicsThread: was healthy=[{}], running=[{}]",
                    healthy_.load(), running_.load());

    // Stop the old thread (safe even if thread already exited)
    Stop();

    // Brief pause to ensure clean shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Restart with the same config
    bool ok = Start(physics_config_, threading_config_, thresholds_config_,
                     log_config_, assets_path_);
    if (!ok) {
        PHYSICS_LOG_ERROR(logger_,"PhysicsThread: recovery failed — Start() returned false");
        return false;
    }

    // Wait for the event loop to initialize the world
    int wait_attempts = 0;
    while (!healthy_.load(std::memory_order_acquire) && wait_attempts < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++wait_attempts;
    }
    if (!healthy_.load(std::memory_order_acquire)) {
        PHYSICS_LOG_ERROR(logger_,"PhysicsThread: recovery failed — "
                         "world did not become healthy after restart");
        return false;
    }

    // Optionally restore state
    if (!saved_state.empty()) {
        if (!world_.RestoreState(saved_state)) {
            PHYSICS_LOG_ERROR(logger_,"PhysicsThread: recovery — "
                             "state restoration failed");
            return false;
        }
        PHYSICS_LOG_INFO(logger_,"PhysicsThread: state restored "
                        "([{}] bytes)", saved_state.size());
    }

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: recovery complete");
    return true;
}

//============================================================================
// EnqueueCommand
//============================================================================

bool PhysicsThread::EnqueueCommand(PhysicsCommand cmd) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    // Frame pile-up protection [D23]: check queue size
    size_t approx_size = command_queue_.size_approx();
    if (static_cast<int>(approx_size) >= threading_config_.max_pending_frames) {
        PHYSICS_LOG_WARN(logger_,"PhysicsThread: command queue full "
                        "(approx=[{}], max=[{}]), dropping command",
                        approx_size, threading_config_.max_pending_frames);
        return false;
    }

    return command_queue_.enqueue(std::move(cmd));
}

//============================================================================
// TryDequeueResult
//============================================================================

std::unique_ptr<PhysicsFrameResult> PhysicsThread::TryDequeueResult() {
    PhysicsFrameResult result;
    if (result_queue_.try_dequeue(result)) {
        return std::make_unique<PhysicsFrameResult>(std::move(result));
    }
    return nullptr;
}

//============================================================================
// VerifyIsPhysicsThread — runtime guard for PT-only code
//============================================================================

void PhysicsThread::VerifyIsPhysicsThread() const {
    // Skip check if physics_thread_id_ hasn't been captured yet
    // (default-constructed thread::id means "not a thread").
    if (physics_thread_id_ != std::thread::id{}) {
        assert(physics_thread_id_ == std::this_thread::get_id()
            && "PhysicsThread: PT-only code called from wrong thread. "
               "This code must only execute on the dedicated physics thread.");
    }
}

//============================================================================
// EventLoop — runs on the dedicated physics thread
//============================================================================

void PhysicsThread::EventLoop() {
    // Capture the physics thread ID once, at the start of the event loop.
    // Used by VerifyIsPhysicsThread() to assert that PT-only code (e.g.
    // PhysicsSystem::UpdateScript) is actually executing on this thread.
    physics_thread_id_ = std::this_thread::get_id();

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: event loop started");

    // Initialize PhysicsWorld with configs captured at Start()
    bool ok = world_.Initialize(physics_config_, threading_config_,
                                 thresholds_config_, logger_, assets_path_);
    if (!ok) {
        PHYSICS_LOG_ERROR(logger_,"PhysicsThread: world initialization failed");
        healthy_.store(false, std::memory_order_release);
        return;
    }
    healthy_.store(true, std::memory_order_release);
    PHYSICS_LOG_INFO(logger_,"PhysicsThread: world initialized, entering event loop");

    // ── Main event loop ──────────────────────────────────────────────
    while (running_.load(std::memory_order_acquire)) {
        PhysicsCommand cmd;
        bool got = command_queue_.try_dequeue(cmd);

        if (!got) {
            std::this_thread::sleep_for(std::chrono::microseconds(50000));  // 50ms
            continue;  // timeout — check running_ flag
        }

        { ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE(cmd);
        try {
            switch (cmd.type) {
            case CommandType::Spawn: {
                auto& args = std::get<SpawnArgs>(cmd.args);
                world_.CreateBody(args.proto_id, args.position,
                                  args.rotation, args.user_data);
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
                // delta=0 is wakeup sentinel — skip simulation
                if (args.delta_time > 0.0f) {
                    PhysicsFrameResult result = world_.Step(
                        args.delta_time, args.frame_id);

                    // Drive Lua collision callbacks on the physics thread
                    // before enqueuing the result for the main thread.
                    if (post_step_callback_) {
                        post_step_callback_(result.collision_events);
                    }

                    { ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE();
                    result_queue_.enqueue(std::move(result));
                    }  // ResultEnqueue slice ends

                    // Frame pile-up protection [D23]: drop oldest if over limit
                    while (result_queue_.size_approx() >
                           static_cast<size_t>(threading_config_.max_pending_frames)) {
                        PhysicsFrameResult dropped;
                        result_queue_.try_dequeue(dropped);
                        PHYSICS_LOG_WARN(logger_,
                            "PhysicsThread: frame pile-up, dropped frame [{}]",
                            dropped.frame_id);
                    }
                }
                break;
            }
            default:
                break;
            }
        } catch (const std::exception& e) {
            PHYSICS_LOG_ERROR(logger_,"PhysicsThread: exception in event loop: {}",
                             e.what());
            healthy_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);
            break;  // exit event loop — world may be in corrupted state
        } catch (...) {
            PHYSICS_LOG_ERROR(logger_,"PhysicsThread: unknown exception in event loop");
            healthy_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);
            break;
        }
        }  // CmdDequeue slice ends
    }

    PHYSICS_LOG_INFO(logger_,"PhysicsThread: event loop exited");
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
