#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/database/data_service/db_request.h"
#include "runtime/database/data_service/db_service_config.h"

namespace engine {
namespace mongo {
class MongoClientPool;
class MongoUri;
}

class DBThread;

// DatabaseService — singleton entry point for async DB operations (R1, R2)
//
// DatabaseService is the ONLY public interface of the data_service module.
// All other classes (DBThread, DBScriptVM) are access-controlled via the
// DATABASE_SERVICE_INTERNAL_ACCESS compile-time guard (R6).
//
// Architecture (matching design §2):
//
//   Main Thread (MT)                DBThread × N (one per core)
//   ┌──────────────────┐           ┌──────────────────────────────┐
//   │ SendRequest()    │──SPSC──→  │ EventLoop()                  │
//   │ PollResponse()   │←──SPSC──  │   Pop client from pool (once)│
//   │                  │           │   ProcessRequest() — blocking │
//   │ (no direct access │          │   EnqueueResponse()           │
//   │  to DBThread/VM) │           │   Push client to pool (once)  │
//   └──────────────────┘           └──────────────────────────────┘
//                                           │
//                               Pop/Push (mutex, once per thread lifetime)
//                                           │
//                               ┌───────────▼──────────────┐
//                               │   MongoClientPool         │
//                               │   (shared, thread-safe)   │
//                               └──────────────────────────┘
//
// MT ↔ DBThread communication via lock-free SPSC queues (moodycamel::ConcurrentQueue).
// Round-robin request distribution via std::atomic counter.
// PollResponse scans all threads' response queues once per call.
//
// Lifecycle (matching design §10):
//   1. MongoSystem::Instance().Initialize()     — mongoc_init, global once
//   2. DatabaseService::Instance().Initialize() — create pool + threads
//   3. Running: SendRequest() / PollResponse()  — MT submits, DBTs process
//   4. DatabaseService::Instance().Shutdown()   — stop threads, drain, destroy pool
//   5. MongoSystem::Instance().Shutdown()       — mongoc_cleanup, global once

class CLOUD_ENGINE_API DatabaseService {
	public:
	static DatabaseService& Instance();

	~DatabaseService();

	DatabaseService(const DatabaseService&) = delete;
	DatabaseService& operator=(const DatabaseService&) = delete;

	// ── Lifecycle (MT exclusive) ───────────────────────────────────────
	//
	// Initialize:
	//   Copies the URI, injects waitQueueTimeoutMS from config, creates a
	//   MongoClientPool, then creates and starts N DBThreads.
	//
	//   Preconditions:
	//     - MongoSystem::Instance().Initialize() must have been called.
	//     - config.thread_pool.thread_count >= 1.
	//
	//   The caller does NOT need to pre-set waitQueueTimeoutMS on the URI
	//   — Initialize handles this internally via uri.Copy() + SetOptionAsInt32
	//   (design §13).
	//
	//   Validation:
	//     - thread_count < 1 → returns false.
	//     - max_pool_size < thread_count → warns (Pop may timeout).
	//
	//   Returns false on: validation failure, pool creation failure, or
	//   any thread Start() failure (already-started threads are stopped).
	//
	// Shutdown:
	//   Gracefully stops all DBThreads (running_=false, wakeup, join),
	//   drains remaining responses from all queues, destroys the pool.
	//   Idempotent — safe to call multiple times.
	//
	//   Postcondition: all borrowed MongoClient* have been returned to pool.
	//   Must be called before MongoSystem::Instance().Shutdown().

	bool Initialize(const DbServiceConfig& config, const mongo::MongoUri& uri);
	void Shutdown();

	// ── Request / Response (thread-safe) ───────────────────────────────
	//
	// SendRequest:
	//   Routes a request to the next DBThread via round-robin (NextThreadIndex).
	//   Returns false if the service is not running, no threads exist, or
	//   the target thread's request queue is full (back-pressure, §6.3).
	//
	// PollResponse:
	//   Scans all DBThread response queues in round-robin order, returning
	//   the first available DbResponse. Returns nullptr if all queues are
	//   empty (non-blocking). Each call scans at most one full circle.

	bool SendRequest(DbRequest&& request);
	std::unique_ptr<DbResponse> PollResponse();

	// ── Status queries (thread-safe) ───────────────────────────────────

	bool IsRunning() const {
		return running_.load(std::memory_order_acquire);
	}
	bool IsHealthy() const;
	int GetThreadCount() const;
	const DbServiceConfig& GetConfig() const {
		return config_;
	}

	// ── Backpressure metrics (thread-safe via std::atomic) ────────────

	uint64_t GetTotalEnqueued() const {
		return total_enqueued_.load(std::memory_order_relaxed);
	}
	uint64_t GetTotalDropped() const {
		return total_dropped_.load(std::memory_order_relaxed);
	}
	uint64_t GetTotalCompleted() const {
		return total_completed_.load(std::memory_order_relaxed);
	}
	uint64_t GetTotalErrors() const {
		return total_errors_.load(std::memory_order_relaxed);
	}

	// Called by DBThread after successful enqueue / completion / error.
	void RecordEnqueue() {
		total_enqueued_.fetch_add(1, std::memory_order_relaxed);
	}
	void RecordDropped() {
		total_dropped_.fetch_add(1, std::memory_order_relaxed);
	}
	void RecordCompleted() {
		total_completed_.fetch_add(1, std::memory_order_relaxed);
	}
	void RecordError() {
		total_errors_.fetch_add(1, std::memory_order_relaxed);
	}

	private:
	DatabaseService();

	// Round-robin thread selection: atomic fetch_add modulo thread count.
	// Thread-safe — MT may call SendRequest from multiple threads, though
	// the common case is a single MT caller.
	int NextThreadIndex();

	mutable std::shared_mutex state_mutex_;
	std::atomic<uint64_t> next_thread_{0};

	// pool_ MUST be declared BEFORE threads_ (LIFO destruction order:
	// threads destroyed first → each ~DBThread calls Stop() → client
	// pushed back to pool → pool destroyed last, cleanly).
	std::unique_ptr<mongo::MongoClientPool> pool_;
	std::vector<std::unique_ptr<DBThread>> threads_;

	DbServiceConfig config_;
	std::atomic<bool> running_{false};

	// PollResponse round-robin cursor. Not atomic — only the MT calls
	// PollResponse, so no concurrent access. Restarts from 0 on each scan
	// to avoid bias toward low-index threads.
	int poll_cursor_ = 0;

	// Backpressure metrics (atomic for thread-safe increment across threads)
	std::atomic<uint64_t> total_enqueued_{0};
	std::atomic<uint64_t> total_dropped_{0};
	std::atomic<uint64_t> total_completed_{0};
	std::atomic<uint64_t> total_errors_{0};
};

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
