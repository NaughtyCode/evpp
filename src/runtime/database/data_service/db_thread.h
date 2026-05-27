#pragma once

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error \
	"db_thread.h is internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including this header."
#endif

#if defined(ENGINE_MONGODB_ENABLED)

#include <atomic>
#include <concurrentqueue.h>
#include <memory>
#include <string>
#include <thread>

#include <quill/Logger.h>

#include "runtime/database/data_service/db_request.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/data_service/db_service_config.h"

namespace engine {
namespace mongo {
class MongoClient;
class MongoClientPool;
}

// ══════════════════════════════════════════════════════════════════════════════
// DBThread — per-thread MongoDB worker (module-internal, R4)
// ══════════════════════════════════════════════════════════════════════════════
//
// Each DBThread runs its own EventLoop on a dedicated std::thread. Inside the
// loop it:
//   - Pops an exclusive MongoClient* from the shared MongoClientPool (blocks
//     on pool_->Pop(), bounded by waitQueueTimeoutMS — R4).
//   - Initialises its private DBScriptVM with log/mongo/runtime bindings.
//   - Dequeues DbRequests from a lock-free SPSC queue (MT → DBT).
//   - Executes CRUD operations synchronously (per mongo-c-driver design).
//   - Enqueues DbResponses back via a second lock-free SPSC queue (DBT → MT).
//   - On shutdown: pushes the MongoClient* back to pool, destroys the VM.
//
// Thread-ownership annotations on members:
//   [MT]   = main-thread exclusive (construction, Start/Stop)
//   [DBT]  = db-thread exclusive (accessed only inside EventLoop)
//   [MT→] = MT initialises, DBT reads (happens-before via atomic store/release)
//   [SPSC] = lock-free SPSC queue (MT ↔ DBT, each side owns one direction)
//   [ATOM] = std::atomic (MT + DBT both access, memory-order controlled)

class DBThread {
	public:
	DBThread(int index, const DbServiceConfig& config);
	~DBThread();

	DBThread(const DBThread&) = delete;
	DBThread& operator=(const DBThread&) = delete;

	// ── Lifecycle (MT calls) ───────────────────────────────────────────
	//
	// Start: creates per-thread Quill logger, saves non-owning pool ref,
	// sets running_ = true, spawns std::thread running EventLoop.
	// Returns false on logger creation or thread spawn failure.
	//
	// Stop: graceful shutdown — sets running_ = false, enqueues a kNoOp
	// wakeup sentinel (unblocks EventLoop from sleep), joins the thread.
	// Idempotent: safe to call multiple times.

	bool Start(mongo::MongoClientPool& pool);
	void Stop();

	// ── SPSC queue operations (MT calls, lock-free) ────────────────────
	//
	// EnqueueRequest: MT → DBT. Returns false if the queue is at capacity
	// (back-pressure protection, see DbThreadPoolConfig::request_queue_size).
	//
	// DequeueResponse: DBT → MT. MT is the sole consumer — no lock needed.
	// Returns nullptr if the response queue is empty (non-blocking).

	bool EnqueueRequest(DbRequest&& req);
	std::unique_ptr<DbResponse> DequeueResponse();

	// ── Status queries ─────────────────────────────────────────────────

	bool IsRunning() const {
		return running_.load(std::memory_order_acquire);
	}
	bool IsHealthy() const {
		return healthy_.load(std::memory_order_acquire);
	}
	int Index() const {
		return index_;
	}
	quill::Logger* GetLogger() const {
		return logger_;
	}
	const DbServiceConfig& GetConfig() const {
		return config_;
	}

	// ── ScriptVM access (module-internal, DBT only) ────────────────────

	DBScriptVM& GetScriptVM() {
		return script_vm_;
	}

	private:
	// ── Logger ─────────────────────────────────────────────────────────
	//
	// Maps DbLogConfig to engine::LogConfig and calls engine::CreateLogger().
	// Logger name: "db_vm_{N}". Output: logs/db_service/db_vm_{N}_<ts>.log.

	quill::Logger* CreateDbLogger();

	// ── Thread main loop + request processing ──────────────────────────
	//
	// EventLoop: full lifecycle — Pop client, init VM/bindings/scripts,
	// enter dequeue-process loop, finally Push client + DestroyScript.
	// See db_thread.cc for the detailed step-by-step flow.
	//
	// ProcessRequest: dispatches DbOperation → C++ CRUD calls or
	// script_vm_.DoString() for kExecuteScript. Handles error extraction
	// from MongoError and cursor serialisation.
	//
	// EnqueueResponse: DBT → MT. Checks capacity and drops oldest if full
	// (response_queue_size gate, §6.3).

	void EventLoop();
	void ProcessRequest(const DbRequest& req);
	void EnqueueResponse(DbResponse&& resp);

	// ════════════════════════════════════════════════════════════════════
	// Members (thread-ownership annotated)
	// ════════════════════════════════════════════════════════════════════

	int index_;	 // [MT] thread index (0..N-1)
	DbServiceConfig config_;  // [MT→] immutable after Start()

	// ── Logger (R9) ────────────────────────────────────────────────────
	quill::Logger* logger_ = nullptr;  // [MT→] created in Start();
	// [MT+DBT] thread-safe writes
	// (Quill loggers are inherently MT-safe)

	// ── MongoDB ────────────────────────────────────────────────────────
	mongo::MongoClientPool* pool_ = nullptr;  // [MT→] non-owning ref to shared pool
	mongo::MongoClient* client_ = nullptr;	// [DBT] exclusive, popped at loop start

	// ── Script VM ──────────────────────────────────────────────────────
	DBScriptVM script_vm_;	// [DBT] exclusive, init'd in EventLoop

	// ── SPSC queues (moodycamel::ConcurrentQueue) ──────────────────────
	moodycamel::ConcurrentQueue<DbRequest>
		request_queue_;	 // [SPSC] MT → DBT (MT: enqueue, DBT: try_dequeue)
	moodycamel::ConcurrentQueue<DbResponse>
		response_queue_;  // [SPSC] DBT → MT (DBT: enqueue, MT: try_dequeue)

	// ── Thread control ─────────────────────────────────────────────────
	std::unique_ptr<std::thread> thread_;  // [MT] lifecycle (spawned in Start, joined in Stop)
	std::atomic<bool> running_{false};	// [ATOM] MT writes (Start/Stop), DBT reads (loop condition)
	std::atomic<bool> healthy_{
		false};	 // [ATOM] DBT writes (init done / error), MT reads (IsHealthy)

	// ── Frame timing ────────────────────────────────────────────────────
	int64_t frame_count_ = 0;  // [DBT] total completed frames, for diagnostics
	std::chrono::steady_clock::time_point
		last_frame_time_;  // [DBT] previous frame end time (for delta)
};

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
