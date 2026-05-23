#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <quill/Logger.h>

#include <concurrentqueue/concurrentqueue.h>

#include "engine/physics/physics_config.h"
#include "engine/physics/physics_commands.h"
#include "engine/physics/physics_world.h"

namespace engine {

//============================================================================
// PhysicsThread — dedicated physics thread with SPSC queues [D18][D19][D20]
//
// Owns the PhysicsWorld, manages the event loop, and communicates with the
// main thread via two moodycamel::ConcurrentQueue instances (command + result).
//============================================================================

class PhysicsThread {
public:
    PhysicsThread() = default;
    ~PhysicsThread();

    PhysicsThread(const PhysicsThread&) = delete;
    PhysicsThread& operator=(const PhysicsThread&) = delete;

    // ── Lifecycle ───────────────────────────────────────────────────────
    // Start the physics thread. Creates independent logger, initializes
    // PhysicsWorld, and enters the event loop.
    // Returns false if the world fails to initialize.
    bool Start(const PhysicsConfig& config,
               const ThreadingConfig& threading,
               const PhysicsLogConfig& log_config,
               const std::string& assets_path);

    // Stop the physics thread gracefully.
    // Signals the thread via atomic flag + wakeup tick, then joins.
    void Stop();

    // ── Main thread interface ──────────────────────────────────────────

    // Enqueue a command for the physics thread. Returns false if the
    // command queue is full (frame pile-up protection [D23]).
    bool EnqueueCommand(PhysicsCommand cmd);

    // Try to dequeue a result from the result queue.
    // Returns nullptr if queue is empty.
    std::unique_ptr<PhysicsFrameResult> TryDequeueResult();

    // Health check — returns false if the thread has crashed [D21].
    bool IsHealthy() const { return healthy_.load(std::memory_order_acquire); }
    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    // Event loop (runs on dedicated thread)
    void EventLoop();

    // Create independent logger instance from PhysicsLogConfig [D7][D8]
    quill::Logger* CreatePhysicsLogger(const PhysicsLogConfig& log_config);

    // ── Members ─────────────────────────────────────────────────────────
    PhysicsWorld world_;
    std::unique_ptr<std::thread> thread_;

    // SPSC queues
    moodycamel::ConcurrentQueue<PhysicsCommand> command_queue_;
    moodycamel::ConcurrentQueue<PhysicsFrameResult> result_queue_;

    // Atomic flags
    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{false};

    // Config copies (stored at Start(), consumed by EventLoop)
    PhysicsConfig physics_config_;
    ThreadingConfig threading_config_;
    std::string assets_path_;

    // Independent logger (owned by physics thread)
    quill::Logger* logger_ = nullptr;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
