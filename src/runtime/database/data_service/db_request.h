#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace engine {

// ══════════════════════════════════════════════════════════════════════════════
// DbRequestStatus — lifecycle status of a database request
// ══════════════════════════════════════════════════════════════════════════════

enum class DbRequestStatus : uint8_t {
	kEnqueued,   // Successfully placed in queue
	kCompleted,  // Execution completed (success or error)
	kDropped,    // Queue full — request discarded
	kTimeout,    // Request exceeded timeout
};

// ══════════════════════════════════════════════════════════════════════════════
// DbOperation — CRUD + script execution operations
// ══════════════════════════════════════════════════════════════════════════════
//
// All operations except kExecuteScript are executed synchronously on the
// DBThread via C++ direct calls to mongo-c-driver (R11).
// kExecuteScript runs a Lua snippet inside the thread's DBScriptVM, giving
// the script full access to the registered mongoc.* / bson.* API surface.
// kNoOp is the wakeup sentinel — see DBThread::Stop().

enum class DbOperation : uint8_t {
	kNoOp = 0,	// sentinel: wakeup / no-op (EventLoop silently discards)
	kFind,	// cursor-based find, limit/skip in DbRequest
	kFindOne,  // single document via cursor limit-1
	kInsertOne,	 // single document insert
	kInsertMany,  // batch insert (bson_data = JSON array of documents)
	kUpdateOne,	 // single document update (filter in bson_data, update desc in bson_data2)
	kUpdateMany,  // multi-document update (same split)
	kDeleteOne,	 // single document delete
	kDeleteMany,  // multi-document delete
	kCount,	 // count documents matching filter
	kAggregate,	 // aggregation pipeline (bson_data or bson_data2 = JSON array of stages)
	kCommand,  // raw MongoDB command (no collection — runs on database or client level)
	kExecuteScript,	 // Lua script execution inside the thread's DBScriptVM (R11)
};

// ══════════════════════════════════════════════════════════════════════════════
// DbRequest — a single database operation sent from MT to a DBThread (SPSC)
// ══════════════════════════════════════════════════════════════════════════════
//
// Lifecycle: constructed on main thread → moved through ConcurrentQueue →
// consumed on DBThread (ProcessRequest), then discarded.
//
// Field usage per operation:
//   kNoOp:          (none)
//   kFind:          database, collection, bson_data (filter), limit, skip
//   kFindOne:       database, collection, bson_data (filter)
//   kInsertOne:     database, collection, bson_data (JSON document)
//   kInsertMany:    database, collection, bson_data (JSON array of documents)
//   kUpdateOne:     database, collection, bson_data (filter), bson_data2 (update descriptor)
//   kUpdateMany:    database, collection, bson_data (filter), bson_data2 (update descriptor)
//   kDeleteOne:     database, collection, bson_data (selector/filter)
//   kDeleteMany:    database, collection, bson_data (selector/filter)
//   kCount:         database, collection, bson_data (filter)
//   kAggregate:     database, collection, bson_data|bson_data2 (pipeline)
//   kCommand:       database, bson_data (command doc)
//   kExecuteScript: script
//
// NOTE: For write operations (kUpdateMany, kDeleteMany), passing an empty
// filter document "{}" will match ALL documents in the collection. Callers
// should provide an explicit, non-empty filter unless a full-collection write
// is genuinely intended.

struct DbRequest {
	uint64_t request_id = 0;  // caller-assigned id, echoed in DbResponse for matching
	DbOperation operation = DbOperation::kNoOp;
	std::string database;  // target database name (required for all CRUD + kCommand)
	std::string collection;	 // target collection name (required for CRUD except kCommand)
	std::string bson_data;	// primary BSON/JSON doc (filter / insert doc / command / pipeline)
	std::string bson_data2;	 // secondary BSON/JSON doc (update descriptor / alt pipeline)
	std::string script;	 // Lua source for kExecuteScript
	int32_t limit = 0;	// kFind: max documents (0 = unlimited)
	int32_t skip = 0;  // kFind: skip first N documents
};

// ══════════════════════════════════════════════════════════════════════════════
// DbResponse — the result of processing one DbRequest
// ══════════════════════════════════════════════════════════════════════════════
//
// result_data serialisation (JSON) per operation:
//   kFind:        "[{doc1},{doc2},...]"          — JSON array of documents
//   kFindOne:     "{doc}" or ""                   — single JSON document (empty if none found)
//   kInsertOne:   "{\"_id\":...}"                 — reply doc from server
//   kInsertMany:  "{\"inserted_count\":N}"        — count of inserted docs
//   kUpdateOne:   ""                              — see affected_count
//   kUpdateMany:  ""                              — see affected_count
//   kDeleteOne:   ""                              — see affected_count
//   kDeleteMany:  ""                              — see affected_count
//   kCount:       "{\"count\":N}"                 — document count
//   kAggregate:   "[{doc1},...]"                  — aggregation result array
//   kCommand:     "{...}"                         — arbitrary server reply
//   kExecuteScript: "..."                         — Lua return value (optional)
//
// error_code is the MongoDB wire-protocol error code (MongoError::Code()).
// affected_count is the number of documents matched/modified/deleted.

struct DbResponse {
	uint64_t request_id = 0;  // matches DbRequest::request_id
	DbRequestStatus status = DbRequestStatus::kEnqueued;
	bool success = false;
	uint32_t error_code = 0;  // MongoDB error code (0 on success)
	std::string error_message;
	std::string result_data;  // JSON-serialised result (format varies by operation)
	int64_t affected_count = 0;	 // nModified (update) or n (delete) from server reply
};

}  // namespace engine
