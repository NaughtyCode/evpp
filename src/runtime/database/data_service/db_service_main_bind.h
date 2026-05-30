#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// ExportDbService — register data-service API into a ScriptVM (main thread)
//
// Registers the following global functions into the given VM so that Lua
// code running on the main thread can interact with the DatabaseService
// singleton. All registered functions are thread-safe and non-blocking.
//
// ── Status queries ──────────────────────────────────────────────────────
//
//   db_is_running()        → boolean
//     Returns true if DatabaseService::IsRunning().
//     False before Initialize() or after Shutdown().
//
//   db_is_healthy()        → boolean
//     Returns true if all DBThreads report healthy.
//     False if any thread has encountered an error in its EventLoop.
//
//   db_get_thread_count()  → integer
//     Returns the number of DBThread workers (0 if not initialized).
//
// ── Request / Response (asynchronous, SPSC-queue based) ──────────────────
//
//   db_send_request(req)   → boolean
//     Builds a DbRequest from the req table and routes it to a DBThread
//     via round-robin. Returns false if the service is not running or
//     the target thread's request queue is full (back-pressure).
//
//     req table fields (all optional except operation):
//       request_id   = <uint64>  (default: 0, echoed in response)
//       operation    = <string|int>  REQUIRED — see operation names below
//       database     = <string>  (required for CRUD + kCommand)
//       collection   = <string>  (required for CRUD except kCommand)
//       bson_data    = <string>  (filter / insert doc / command / pipeline)
//       bson_data2   = <string>  (update descriptor / alt pipeline)
//       script       = <string>  (required for "execute_script")
//       limit        = <int>     (kFind: max documents, 0 = unlimited)
//       skip         = <int>     (kFind: skip first N documents)
//       allow_empty_filter = <bool> (delete ops: explicit "{}" confirmation)
//
//     Operation names (string form, case-insensitive):
//       "find"           → kFind
//       "find_one"       → kFindOne
//       "insert_one"     → kInsertOne
//       "insert_many"    → kInsertMany
//       "update_one"     → kUpdateOne
//       "update_many"    → kUpdateMany
//       "delete_one"     → kDeleteOne
//       "delete_many"    → kDeleteMany
//       "count"          → kCount
//       "aggregate"      → kAggregate
//       "command"        → kCommand
//       "execute_script" → kExecuteScript
//     Integer values matching the DbOperation enum are also accepted.
//
//   db_poll_response()    → table | nil
//     Scans all DBThread response queues in round-robin order and returns
//     the first available response as a table, or nil if all queues are
//     empty (non-blocking). Call once per frame to collect results.
//
//     Response table fields:
//       request_id     = <uint64>
//       success        = <boolean>
//       error_code     = <uint>      (MongoDB wire-protocol error code)
//       error_message  = <string>
//       result_data    = <string>    (JSON, format varies per operation)
//       affected_count = <int>       (documents matched/modified/deleted)
//
// ── Example ──────────────────────────────────────────────────────────────
//
//   -- Check service health before sending
//   if not db_is_healthy() then
//       log_error("DB service is unhealthy, skipping query")
//       return
//   end
//
//   -- Send a find request
//   local ok = db_send_request({
//       request_id = 1001,
//       operation = "find",
//       database = "game_db",
//       collection = "players",
//       bson_data = '{"score": {"$gt": 100}}',
//       limit = 10,
//   })
//   if not ok then
//       log_warn("db_send_request failed: queue full or service stopped")
//   end
//
//   -- Poll response in a later frame
//   local resp = db_poll_response()
//   if resp then
//       if resp.success then
//           log_info("got " .. resp.result_data)
//       else
//           log_error("DB error: " .. resp.error_message)
//       end
//   end
//
// ── Usage in engine initialisation ───────────────────────────────────────
//
//   Called via script::ExportAll(vm) after Engine creates its main VM.
//   Guarded by ENGINE_MONGODB_ENABLED so it compiles out when MongoDB
//   support is disabled.
//

CLOUD_ENGINE_API void ExportDbService(ScriptVM& vm);

}  // namespace script
}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
