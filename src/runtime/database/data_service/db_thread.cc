#if defined(ENGINE_MONGODB_ENABLED)

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/db_thread.h"

#include <chrono>

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bson_ext.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_forward.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
#include "runtime/script/import_bind.h"

namespace engine {

// ══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// SerializeCursor — iterate a MongoCursor and build a JSON array string.
//
// Traverses the cursor, optionally skipping the first `skip` documents,
// and concatenates each document's JSON representation into a "[...]" array.
// Returns "[]" for an empty cursor.
//
// The cursor must have been obtained from a FindWithOpts / Aggregate call.
// Caller is responsible for cursor->Destroy() after this returns.

std::string SerializeCursor(mongo::MongoCursor* cursor, int32_t skip) {
	std::string result = "[";
	bool first = true;
	while (true) {
		mongo::BsonDocument doc;
		if (!cursor->Next(&doc)) break;
		if (skip > 0) {
			--skip;
			continue;
		}
		if (!first) result += ",";
		result += doc.ToJson();
		first = false;
	}
	result += "]";
	return result;
}

// Parse a JSON string into a BsonDocument, returning true on success.
// On failure (invalid JSON), sets the error on *resp and returns false.
// Empty input is treated as valid — callers validate emptiness separately
// with more specific error messages.
// Uses InitFromJson with a MongoError to reliably distinguish parse
// failures from valid empty documents like "{}" or "[]".
bool ParseJsonDoc(const std::string& json_str,
				  const char* field_name,
				  mongo::BsonDocument* out,
				  DbResponse* resp) {
	if (json_str.empty()) return true;
	mongo::MongoError err;
	if (!out->InitFromJson(json_str.c_str(), static_cast<int64_t>(json_str.size()), &err)) {
		resp->success = false;
		resp->error_message = std::string("invalid JSON in ") + field_name + ": " + err.Message();
		return false;
	}
	return true;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════════════════════

DBThread::DBThread(int index, const DbServiceConfig& config)
	: index_(index), config_(config), last_frame_time_(std::chrono::steady_clock::time_point{}) {
}

DBThread::~DBThread() {
	Stop();
}

// ══════════════════════════════════════════════════════════════════════════════
// Logger (R9)
// ══════════════════════════════════════════════════════════════════════════════
//
// Maps DbLogConfig fields to engine::LogConfig and delegates to CreateLogger().
// The resulting logger writes to logs/db_service/db_vm_{N}_<timestamp>.log.
// Rotation is purely size-based (rotation_frequency = "").

quill::Logger* DBThread::CreateDbLogger() {
	LogConfig mapped;
	mapped.logger_name = "db_vm_" + std::to_string(index_);
	mapped.log_filename = "";
	mapped.dir = config_.log.dir;
	mapped.level = config_.log.level;
	mapped.rotation_size_mb = config_.log.rotation_size_mb;
	mapped.max_backup_files = config_.log.max_backup_files;
	mapped.rotation_frequency = "";
	mapped.rotation_interval = 1;
	mapped.rotation_time_daily = "00:00";
	return CreateLogger(mapped);
}

// ══════════════════════════════════════════════════════════════════════════════
// Lifecycle (MT-callable)
// ══════════════════════════════════════════════════════════════════════════════

bool DBThread::Start(mongo::MongoClientPool& pool) {
	logger_ = CreateDbLogger();
	if (!logger_) {
		std::fprintf(stderr, "DBThread[%d]: failed to create logger\n", index_);
		return false;
	}

	pool_ = &pool;
	running_.store(true, std::memory_order_release);

	try {
		thread_ = std::make_unique<std::thread>(&DBThread::EventLoop, this);
	} catch (const std::exception& e) {
		ENGINE_LOG_ERROR(logger_, "DBThread[{}]: failed to start thread: {}", index_, e.what());
		running_.store(false, std::memory_order_release);
		return false;
	}

	return true;
}

void DBThread::Stop() {
	// Signal the EventLoop to shut down. Always store, even if the thread
	// has already exited — the store is cheap and ensures the signal is
	// visible regardless of which code path the thread took.
	running_.store(false, std::memory_order_release);

	// Wakeup sentinel: enqueue a kNoOp so EventLoop breaks out of sleep.
	// If the thread is still blocked in pool_->Pop() (init phase), the
	// sentinel sits in the queue until the thread enters the main loop.
	// In that case Shutdown latency is bounded by waitQueueTimeoutMS —
	// Pop() will return nullptr on timeout, the thread exits init, finds
	// running_==false, and the sentinel is drained during the next cycle.
	// If the thread already exited, the sentinel is harmless — it will be
	// discarded when the DBThread is destroyed.
	DbRequest wakeup;
	wakeup.operation = DbOperation::kNoOp;
	request_queue_.enqueue(std::move(wakeup));

	// Always join if the thread is valid and joinable.  This covers both
	// the normal exit path and the early-return path (pool Pop timeout).
	// joinable() returns false after the first join, making Stop() safe
	// to call multiple times (idempotent).
	if (thread_ && thread_->joinable()) {
		thread_->join();
	}
	thread_.reset();
}

// ══════════════════════════════════════════════════════════════════════════════
// SPSC queue operations (MT-callable, lock-free)
// ══════════════════════════════════════════════════════════════════════════════

bool DBThread::EnqueueRequest(DbRequest&& req) {
	if (!running_.load(std::memory_order_acquire)) return false;

	// Back-pressure: reject if approximate queue size >= configured max.
	// size_approx() is a O(1) best-effort snapshot — moodycamel docs say
	// it may undercount but never overcount beyond a small epsilon.
	// This is acceptable; the alternative (exact count) requires a mutex.
	size_t sz = request_queue_.size_approx();
	if (sz >= static_cast<size_t>(config_.thread_pool.request_queue_size)) {
		ENGINE_LOG_WARN(logger_,
						"DBThread[{}]: request queue full (approx={}, max={})",
						index_,
						sz,
						config_.thread_pool.request_queue_size);
		return false;
	}
	return request_queue_.enqueue(std::move(req));
}

std::unique_ptr<DbResponse> DBThread::DequeueResponse() {
	DbResponse resp;
	if (response_queue_.try_dequeue(resp)) {
		return std::make_unique<DbResponse>(std::move(resp));
	}
	return nullptr;
}

// ══════════════════════════════════════════════════════════════════════════════
// EnqueueResponse — DBT → MT, with capacity check (§6.3)
// ══════════════════════════════════════════════════════════════════════════════
//
// If the response queue is full, the oldest response is silently dropped.
// This is a deliberate trade-off: blocking the DBThread to wait for MT to
// drain responses would stall all DB processing for this thread. The dropped
// response's request_id is logged at WARN level for diagnostics.

void DBThread::EnqueueResponse(DbResponse&& resp) {
	// Drop oldest responses while the queue is at capacity.  The retry
	// counter guards against a theoretical infinite loop when size_approx()
	// overcounts and try_dequeue keeps failing — in practice size_approx()
	// is reliable within a small epsilon, so the limit is never hit.
	int retries = 0;
	while (response_queue_.size_approx() >=
			   static_cast<size_t>(config_.thread_pool.response_queue_size) &&
		   retries < 5) {
		DbResponse dropped;
		if (response_queue_.try_dequeue(dropped)) {
			ENGINE_LOG_WARN(logger_,
							"DBThread[{}]: response queue full, dropped response [id={}]",
							index_,
							dropped.request_id);
		}
		++retries;
	}
	response_queue_.enqueue(std::move(resp));
}

// ══════════════════════════════════════════════════════════════════════════════
// EventLoop — the DBThread's main function (§6.1)
// ══════════════════════════════════════════════════════════════════════════════
//
// Lifecycle phases (matching design §6.1):
//
//   1. Pop client from pool (blocking, bounded by waitQueueTimeoutMS).
//      On timeout: log error, set running_=false, return (healthy_ stays false).
//
//   2. Init phase (try block, caught by outer catch):
//      a. RegisterSubsystemObjects — store DBThread/MongoClient/MongoClientPool
//         pointers in the VM's CustomPtrStore (DbCustomPtr slots 1-4).
//      b. ExportDbLog — register log_* globals bound to this thread's logger (R9).
//      c. script::ExportMongo — create mongoc / bson global module tables (R10).
//      d. Register modules in package.loaded so require() works:
//           package.loaded.mongoc = mongoc
//           package.loaded.bson   = bson
//      e. ExportDbRuntime — register db_get_client / db_get_pool globals.
//      f. SetImportPath — configure import search paths (R12):
//           db_scripts_dir first, then runtime_scripts_dir.
//      f2. ExportImport — register the import() Lua global function.
//      g. If auto_load: DoDirectory(runtime) → DoDirectory(db_scripts).
//      h. InitScript().
//      i. Set healthy_ = true.
//
//   3. Main loop (while running_):
//      Each iteration = one "frame". Drains all available requests
//      (up to max_requests_per_frame if configured). After processing,
//      calls script_vm_.CallFrameCallback() → Lua global on_db_frame(info).
//      Frame rate is maintained via target_fps — sleeps at end of frame
//      to match the target rate; overruns are logged at WARN level.
//      - target_fps = 0: unlimited mode, 1ms idle sleep only when queue empty.
//      - Empty queue → sleep to avoid busy-wait.
//      - Catch std::exception / ... → log, mark unhealthy, break to cleanup.
//
//   4. Cleanup (always executed, regardless of exit path):
//      - healthy_ = false.
//      - DestroyScript().     (ScriptVM cleanup: GC, close Lua state)
//      - Push client to pool. (must balance the Pop from phase 1)
//      - running_ = false.    (signals MT that thread has fully exited)
//
// Exception safety: the outer try/catch for init-phase errors and the inner
// try/catch for loop errors both funnel into the same cleanup block. The
// Pop'd client is always returned to the pool (or was never obtained on
// init-phase failure).

void DBThread::EventLoop() {
	// ── Phase 1: Acquire MongoClient from shared pool ──────────────────
	client_ = pool_->Pop();
	if (!client_) {
		ENGINE_LOG_ERROR(logger_, "DBThread[{}]: pool Pop failed (timeout or pool closed)", index_);
		running_.store(false, std::memory_order_release);
		return;
	}

	try {
		// ── Phase 2: Init — register bindings and load scripts ─────────

		// 2a. Register subsystem object pointers (CustomPtrStore slots 1-4)
		script_vm_.RegisterSubsystemObjects(this, client_, pool_);

		// 2b. Per-thread log functions bound to this thread's Quill logger (R9)
		ExportDbLog(script_vm_, logger_);

		// 2c. MongoDB API bindings — mongoc.* / bson.* global tables (R10)
		script::ExportMongo(script_vm_);

		// 2d. Wire global tables into the module system so require() works
		if (!script_vm_.DoString("package.loaded.mongoc = mongoc; "
								 "package.loaded.bson   = bson")) {
			ENGINE_LOG_WARN(
				logger_, "DBThread[{}]: failed to register mongoc/bson in package.loaded", index_);
		}

		// 2e. db_get_client / db_get_pool — access CustomPtr slots from Lua
		ExportDbRuntime(script_vm_);

		// 2f. Configure import path — db_scripts_dir searched first (R12)
		script_vm_.SetImportPath(config_.script.db_scripts_dir + ";" +
								 config_.script.runtime_scripts_dir);

		// 2f2. Register the import() Lua global so scripts can use import("module")
		ExportImport(script_vm_);

		// 2g. Load scripts: runtime first (shared), then db_service (can override) (R12)
		if (config_.script.auto_load) {
			script_vm_.DoDirectory(config_.script.runtime_scripts_dir);
			script_vm_.DoDirectory(config_.script.db_scripts_dir);
		}
		// 2h. Call user-defined init hooks
		script_vm_.InitScript();

		// 2i. Signal readiness to MT
		healthy_.store(true, std::memory_order_release);

		// ── Phase 3: Main loop (frame-aware) ──────────────────────────
		//
		// Each iteration is one "frame". Within a frame the thread
		// drains as many requests as possible (up to max_requests_per_frame,
		// or unlimited if 0). Frame rate is gated by target_fps:
		//   target_fps = 0  → unlimited, 1ms idle sleep to prevent CPU spin
		//   target_fps > 0  → sleep at end of frame to maintain target rate
		//
		// Exception safety: any exception during request processing
		// terminates the loop (same as before). This is deliberate —
		// ProcessRequest has its own internal try/catch, so exceptions
		// reaching here indicate a fatal VM or system error.

		auto frame_interval = std::chrono::microseconds(0);
		if (config_.thread_pool.target_fps > 0) {
			frame_interval = std::chrono::microseconds(1000000 / config_.thread_pool.target_fps);
		}

		last_frame_time_ = std::chrono::steady_clock::now();

		while (running_.load(std::memory_order_acquire)) {
			auto frame_start = std::chrono::steady_clock::now();
			int processed = 0;
			int max_per_frame = config_.thread_pool.max_requests_per_frame;

			try {
				DbRequest req;
				while (request_queue_.try_dequeue(req)) {
					if (req.operation != DbOperation::kNoOp) {
						ProcessRequest(req);
					}
					// kNoOp is silently discarded — it's the wakeup sentinel
					processed++;
					if (max_per_frame > 0 && processed >= max_per_frame) {
						break;
					}
				}
			} catch (const std::exception& e) {
				ENGINE_LOG_ERROR(
					logger_, "DBThread[{}]: exception in event loop: {}", index_, e.what());
				healthy_.store(false, std::memory_order_release);
				running_.store(false, std::memory_order_release);
				break;
			} catch (...) {
				ENGINE_LOG_ERROR(logger_, "DBThread[{}]: unknown exception in event loop", index_);
				healthy_.store(false, std::memory_order_release);
				running_.store(false, std::memory_order_release);
				break;
			}

			frame_count_++;

			// ── Per-frame Lua callback ────────────────────────────
			{
				auto now = std::chrono::steady_clock::now();
				double delta = 0.0;
				if (frame_count_ > 1) {
					delta = std::chrono::duration<double>(now - last_frame_time_).count();
				}
				last_frame_time_ = now;
				script_vm_.CallFrameCallback(frame_count_, delta);
			}

			// ── Frame rate control ─────────────────────────────────
			if (config_.thread_pool.target_fps > 0) {
				auto elapsed = std::chrono::steady_clock::now() - frame_start;
				if (elapsed < frame_interval) {
					std::this_thread::sleep_for(frame_interval - elapsed);
				} else if (processed > 0 && elapsed > frame_interval * 2) {
					ENGINE_LOG_WARN(
						logger_,
						"DBThread[{}]: frame overrun, elapsed={}us budget={}us "
						"processed={} frame={}",
						index_,
						std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
						frame_interval.count(),
						processed,
						frame_count_);
				}
			} else if (processed == 0) {
				// Unlimited mode: minimal sleep when idle to avoid
				// busy-wait. 1ms keeps latency low while preventing
				// 100% CPU spin on an empty queue.
				std::this_thread::sleep_for(std::chrono::microseconds(1000));
			}
		}
	} catch (const std::exception& e) {
		ENGINE_LOG_ERROR(logger_, "DBThread[{}]: exception in event loop: {}", index_, e.what());
	} catch (...) {
		ENGINE_LOG_ERROR(logger_, "DBThread[{}]: unknown exception in event loop", index_);
	}

	// ── Phase 4: Cleanup (always reached) ──────────────────────────────
	healthy_.store(false, std::memory_order_release);
	script_vm_.DestroyScript();
	if (client_) {
		pool_->Push(client_);
		client_ = nullptr;
	}
	running_.store(false, std::memory_order_release);
}

// ══════════════════════════════════════════════════════════════════════════════
// ProcessRequest — dispatch DbOperation to the appropriate handler (§6.4)
// ══════════════════════════════════════════════════════════════════════════════
//
// Two code paths:
//
//   kExecuteScript:
//     Runs the Lua script inside this thread's DBScriptVM (via DoString).
//     No database/collection handles are created — the script uses the
//     exported mongoc.* / db_get_client() API to access MongoDB directly.
//     DoString returns false on Lua error; the error message is captured
//     in resp.error_message.
//
//   CRUD (all other operations):
//     Executes synchronously via C++ direct calls to mongo-c-driver.
//     Collection handles are obtained from client_ on demand for operations
//     that target a collection. kCommand calls client_->CommandSimple()
//     directly without intermediate handles. All obtained handles are
//     destroyed before the response is enqueued (RAII-by-manual-cleanup).
//
// Input validation (applied before dispatch):
//   - CRUD operations (non-kCommand, non-kExecuteScript): database and
//     collection must be non-empty.
//   - Operations requiring a filter (kFind, kFindOne, kUpdate*, kDelete*,
//     kCount): bson_data must be non-empty.
//   - Update operations (kUpdateOne, kUpdateMany): bson_data2 (the update
//     descriptor) must be non-empty.
//   - kInsertOne: bson_data must be non-empty.
//   - kInsertMany: bson_data must be non-empty.
//   - kCommand: database and bson_data must be non-empty.
//   - kAggregate: bson_data or bson_data2 (pipeline) must be non-empty.
//   - kExecuteScript: script must be non-empty.
//
// Error handling:
//   - MongoError is checked after every CRUD call. On failure, error_code
//     and error_message are extracted to the response.
//   - std::exception is caught at the top-level try/catch. In that case the
//     response carries exception.what() but no error_code.
//   - Database/collection handle cleanup happens outside the try block —
//     nullptr checks ensure safety even if handles were never obtained.

void DBThread::ProcessRequest(const DbRequest& req) {
	DbResponse resp;
	resp.request_id = req.request_id;

	// kNoOp should have been filtered by EventLoop; defensive early-return.
	if (req.operation == DbOperation::kNoOp) return;

	// ── Common input validation ────────────────────────────────────────
	//
	// CRUD operations (all except kCommand and kExecuteScript) need both
	// database and collection. kCommand needs database only.
	// kExecuteScript needs none — the script accesses the DB via exported APIs.

	if (req.operation != DbOperation::kExecuteScript && req.operation != DbOperation::kCommand) {
		if (req.database.empty() || req.collection.empty()) {
			resp.success = false;
			resp.error_message = "database and collection required for CRUD operations";
			EnqueueResponse(std::move(resp));
			return;
		}
	}

	// ── kExecuteScript path (no db/coll handles needed) ────────────────

	if (req.operation == DbOperation::kExecuteScript) {
		if (req.script.empty()) {
			resp.success = false;
			resp.error_message = "script content required for kExecuteScript";
			EnqueueResponse(std::move(resp));
			return;
		}
		try {
			std::string script_result;
			resp.success =
				script_vm_.DoString(req.script, "db_request", &resp.error_message, &script_result);
			if (!script_result.empty()) resp.result_data = script_result;
		} catch (const std::exception& e) {
			resp.success = false;
			resp.error_message = e.what();
		}
		EnqueueResponse(std::move(resp));
		return;
	}

	// ── CRUD path — obtain handles and dispatch ────────────────────────

	mongo::MongoCollection* coll = nullptr;

	try {
		// kCommand uses client_->CommandSimple(db_name, ...) directly,
		// so no standalone db handle is needed. All other CRUD ops need
		// a collection handle obtained from the client.
		bool need_coll = (req.operation != DbOperation::kCommand);

		if (need_coll && !req.collection.empty()) {
			coll = client_->GetCollection(req.database.c_str(), req.collection.c_str());
			if (!coll) {
				resp.success = false;
				resp.error_message = "failed to obtain collection handle";
				EnqueueResponse(std::move(resp));
				return;
			}
		}

		// Validate bson_data (filter) for operations that require it
		bool need_filter =
			(req.operation == DbOperation::kFind || req.operation == DbOperation::kFindOne ||
			 req.operation == DbOperation::kUpdateOne ||
			 req.operation == DbOperation::kUpdateMany ||
			 req.operation == DbOperation::kDeleteOne ||
			 req.operation == DbOperation::kDeleteMany || req.operation == DbOperation::kCount);
		if (need_filter && req.bson_data.empty()) {
			resp.success = false;
			resp.error_message = "bson_data (filter) required for this operation";
			if (coll) coll->Destroy();
			EnqueueResponse(std::move(resp));
			return;
		}

		// Validate bson_data2 (update descriptor) for update operations.
		// The update descriptor (e.g. {"$set": {"field": "value"}}) is mandatory
		// for kUpdateOne / kUpdateMany.
		bool need_update_doc =
			(req.operation == DbOperation::kUpdateOne || req.operation == DbOperation::kUpdateMany);
		if (need_update_doc && req.bson_data2.empty()) {
			resp.success = false;
			resp.error_message = "bson_data2 (update descriptor) required for update operations";
			if (coll) coll->Destroy();
			EnqueueResponse(std::move(resp));
			return;
		}

		switch (req.operation) {
		// ── kFind: cursor-based query with optional limit/skip ──────────
		//
		// skip and limit are passed to FindWithOpts via the opts BSON so
		// they match MongoDB native semantics: skip is applied first on
		// the server, then limit caps the returned documents.  Previously
		// skip was applied in SerializeCursor AFTER the cursor limit,
		// which caused skip=5,limit=10 to return only 5 documents.
		case DbOperation::kFind: {
			mongo::BsonDocument filter;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &filter, &resp)) break;

			mongo::BsonDocument opts;
			bool has_opts = false;
			if (req.skip > 0) {
				opts.AppendInt32("skip", req.skip);
				has_opts = true;
			}
			if (req.limit > 0) {
				opts.AppendInt32("limit", req.limit);
				has_opts = true;
			}

			auto* cursor = coll->FindWithOpts(filter, has_opts ? &opts : nullptr, nullptr);
			if (!cursor) {
				resp.success = false;
				resp.error_message = "failed to create find cursor";
				break;
			}
			try {
				resp.result_data = SerializeCursor(cursor, 0);
				resp.success = true;
			} catch (...) {
				cursor->Destroy();
				throw;
			}
			cursor->Destroy();
			break;
		}

		// ── kFindOne: cursor with limit 1, returns single doc or "" ─────
		case DbOperation::kFindOne: {
			mongo::BsonDocument filter;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &filter, &resp)) break;
			auto* cursor = coll->FindWithOpts(filter, nullptr, nullptr);
			if (!cursor) {
				resp.success = false;
				resp.error_message = "failed to create find cursor";
				break;
			}
			try {
				cursor->SetLimit(1);
				mongo::BsonDocument doc;
				if (cursor->Next(&doc)) resp.result_data = doc.ToJson();
				resp.success = true;
			} catch (...) {
				cursor->Destroy();
				throw;
			}
			cursor->Destroy();
			break;
		}

		// ── kInsertOne: single document insert ──────────────────────────
		case DbOperation::kInsertOne: {
			if (req.bson_data.empty()) {
				resp.success = false;
				resp.error_message = "bson_data (JSON document) required for InsertOne";
				break;
			}
			mongo::BsonDocument doc;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &doc, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success = coll->InsertOne(doc, nullptr, &reply, &err);
			if (resp.success) {
				resp.result_data = reply.ToJson();
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kInsertMany: batch insert from JSON array ───────────────────
		// bson_data must be a JSON array [{...}, {...}, ...].
		// BSON represents arrays as documents with integer keys ("0","1",...).
		case DbOperation::kInsertMany: {
			if (req.bson_data.empty()) {
				resp.success = false;
				resp.error_message = "bson_data (JSON array of documents) required for InsertMany";
				break;
			}
			std::vector<mongo::BsonDocument> docs;
			std::vector<const mongo::BsonDocument*> doc_ptrs;

			mongo::BsonDocument arr;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &arr, &resp)) break;
			mongo::BsonIter iter(arr);
			while (iter.Next()) {
				if (iter.Type() != static_cast<int>(mongo::BsonType::kDocument)) continue;
				uint32_t len = 0;
				const uint8_t* data = nullptr;
				iter.AsDocument(&len, &data);
				docs.push_back(mongo::BsonDocument(data, len));
			}

			// Rebuild doc_ptrs after the loop — docs may have reallocated
			// during push_back, invalidating earlier &docs.back() pointers.
			doc_ptrs.reserve(docs.size());
			for (auto& d : docs) {
				doc_ptrs.push_back(&d);
			}

			if (!doc_ptrs.empty()) {
				mongo::BsonDocument reply;
				mongo::MongoError err;
				resp.success =
					coll->InsertMany(doc_ptrs.data(), doc_ptrs.size(), nullptr, &reply, &err);
				if (resp.success) {
					int64_t inserted = static_cast<int64_t>(doc_ptrs.size());
					mongo::BsonIter reply_iter;
					if (reply_iter.InitFind(reply, "n")) {
						inserted = reply_iter.AsInt64();
					}
					resp.result_data = "{\"inserted_count\":" + std::to_string(inserted) + "}";
				} else {
					resp.error_code = err.Code();
					resp.error_message = err.Message();
				}
			} else {
				resp.success = false;
				resp.error_message = "no documents to insert";
			}
			break;
		}

		// ── kUpdateOne: filter in bson_data, update descriptor in bson_data2
		// affected_count extracted from reply.nModified via BsonIter.
		case DbOperation::kUpdateOne: {
			mongo::BsonDocument filter;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &filter, &resp)) break;
			mongo::BsonDocument update;
			if (!ParseJsonDoc(req.bson_data2, "bson_data2", &update, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success = coll->UpdateOne(filter, update, nullptr, &reply, &err);
			if (resp.success) {
				mongo::BsonIter iter;
				if (iter.InitFind(reply, "nModified")) {
					resp.affected_count = iter.AsInt64();
				}
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kUpdateMany: same pattern as kUpdateOne ─────────────────────
		case DbOperation::kUpdateMany: {
			mongo::BsonDocument filter;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &filter, &resp)) break;
			mongo::BsonDocument update;
			if (!ParseJsonDoc(req.bson_data2, "bson_data2", &update, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success = coll->UpdateMany(filter, update, nullptr, &reply, &err);
			if (resp.success) {
				mongo::BsonIter iter;
				if (iter.InitFind(reply, "nModified")) {
					resp.affected_count = iter.AsInt64();
				}
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kDeleteOne: filter in bson_data, affected_count from reply.n ─
		case DbOperation::kDeleteOne: {
			mongo::BsonDocument selector;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &selector, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success = coll->DeleteOne(selector, nullptr, &reply, &err);
			if (resp.success) {
				mongo::BsonIter iter;
				if (iter.InitFind(reply, "n")) {
					resp.affected_count = iter.AsInt64();
				}
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kDeleteMany: same pattern as kDeleteOne ─────────────────────
		//
		// SAFETY NOTE: An empty filter "{}" matches ALL documents in the
		// collection. The input validation above ensures bson_data is
		// non-empty, but a caller could still pass "{}" as a valid JSON
		// filter. This is accepted as intentional — the caller is
		// responsible for providing a restrictive filter unless a
		// full-collection delete is genuinely intended.
		case DbOperation::kDeleteMany: {
			mongo::BsonDocument selector;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &selector, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success = coll->DeleteMany(selector, nullptr, &reply, &err);
			if (resp.success) {
				mongo::BsonIter iter;
				if (iter.InitFind(reply, "n")) {
					resp.affected_count = iter.AsInt64();
				}
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kCount: count documents matching filter ─────────────────────
		// CountDocuments returns -1 on error (checked via err param).
		case DbOperation::kCount: {
			mongo::BsonDocument filter;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &filter, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			int64_t count = coll->CountDocuments(filter, nullptr, nullptr, &reply, &err);
			if (count >= 0) {
				resp.success = true;
				resp.result_data = "{\"count\":" + std::to_string(count) + "}";
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		// ── kAggregate: pipeline from bson_data (preferred) or bson_data2 ─
		//
		// Accepts a JSON array of pipeline stages [{$match:...},{$group:...}].
		// BSON arrays are documents with integer keys ("0","1",...), so
		// NewFromJson on a JSON array produces the correct BSON representation
		// that mongoc_collection_aggregate expects.
		case DbOperation::kAggregate: {
			const char* pipe_field = req.bson_data.empty() ? "bson_data2" : "bson_data";
			const std::string& pipe_json = req.bson_data.empty() ? req.bson_data2 : req.bson_data;
			if (pipe_json.empty()) {
				resp.success = false;
				resp.error_message = "bson_data or bson_data2 (pipeline) required for aggregate";
				break;
			}
			mongo::BsonDocument pipeline;
			if (!ParseJsonDoc(pipe_json, pipe_field, &pipeline, &resp)) break;
			auto* cursor = coll->Aggregate(pipeline, nullptr, nullptr);
			if (!cursor) {
				resp.success = false;
				resp.error_message = "failed to create aggregate cursor";
				break;
			}
			try {
				resp.result_data = SerializeCursor(cursor, 0);
				resp.success = true;
			} catch (...) {
				cursor->Destroy();
				throw;
			}
			cursor->Destroy();
			break;
		}

		// ── kCommand: raw MongoDB command on a specific database ─────────
		//
		// Executed via client_->CommandSimple(req.database, command, ...)
		// which sends the command to the database named in the request.
		// bson_data is the command document (e.g. {"ping": 1}, {"buildInfo": 1}).
		case DbOperation::kCommand: {
			if (req.database.empty() || req.bson_data.empty()) {
				resp.success = false;
				resp.error_message = "database and bson_data (command) required for Command";
				break;
			}
			mongo::BsonDocument command;
			if (!ParseJsonDoc(req.bson_data, "bson_data", &command, &resp)) break;
			mongo::BsonDocument reply;
			mongo::MongoError err;
			resp.success =
				client_->CommandSimple(req.database.c_str(), command, nullptr, &reply, &err);
			if (resp.success) {
				resp.result_data = reply.ToJson();
			} else {
				resp.error_code = err.Code();
				resp.error_message = err.Message();
			}
			break;
		}

		default:
			resp.success = false;
			resp.error_message = "unsupported operation";
			break;
		}
	} catch (const std::exception& e) {
		resp.success = false;
		resp.error_message = e.what();
	} catch (...) {
		resp.success = false;
		resp.error_message = "unknown exception in ProcessRequest";
	}

	// RAII cleanup: destroy handles if they were obtained.
	// nullptr checks are safe — Destroy() on null is a no-op per mongo wrapper.
	if (coll) coll->Destroy();

	EnqueueResponse(std::move(resp));
}

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
