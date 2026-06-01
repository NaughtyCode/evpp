#if defined(ENGINE_MONGODB_ENABLED)

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/db_script_vm.h"

#include "runtime/core/log/log.h"
#include "runtime/database/data_service/bson_table_codec.h"
#include "runtime/database/data_service/db_thread.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
#include "runtime/vm/custom_ptr_store.h"
#include "runtime/vm/lua_error_handler.h"

namespace engine {

// Internal log helpers (anonymous namespace)
//
// Each l_db_log_* function is a Lua C closure with one upvalue: the DBThread's
// Quill logger pointer (lightuserdata). Lua calls like log_info("msg") route
// through these to write to logs/db_service/db_vm_{N}.log.
//
// The [lua] prefix in the format string distinguishes Lua-originated log lines
// from native C++ log output.

namespace {

int l_db_log_trace(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_TRACE(logger, "[lua] {}", msg);
	return 0;
}

int l_db_log_debug(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_DEBUG(logger, "[lua] {}", msg);
	return 0;
}

int l_db_log_info(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_INFO(logger, "[lua] {}", msg);
	return 0;
}

int l_db_log_warn(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_WARN(logger, "[lua] {}", msg);
	return 0;
}

int l_db_log_error(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_ERROR(logger, "[lua] {}", msg);
	return 0;
}

int l_db_log_fatal(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	auto* logger = static_cast<quill::Logger*>(lua_touserdata(L, lua_upvalueindex(1)));
	ENGINE_LOG_CRITICAL(logger, "[lua] {}", msg);
	return 0;
}

// db_get_client / db_get_pool helpers
//
// Both closures capture the DBScriptVM* as an upvalue (lightuserdata) and
// read the corresponding CustomPtr slot. The C++ pointer is returned to Lua
// as lightuserdata — the mongoc.* bindings (mongo_bind.cc) know how to
// unwrap it and construct collection/database handles.

int l_db_get_client(lua_State* L) {
	auto& vm = *static_cast<DBScriptVM*>(lua_touserdata(L, lua_upvalueindex(1)));
	auto* client = vm.GetMongoClient();
	if (!client) {
		lua_pushnil(L);
		lua_pushstring(L, "MongoClient not available");
		return 2;
	}
	lua_pushlightuserdata(L, static_cast<void*>(client));
	return 1;
}

int l_db_get_pool(lua_State* L) {
	auto& vm = *static_cast<DBScriptVM*>(lua_touserdata(L, lua_upvalueindex(1)));
	auto* pool = vm.GetMongoClientPool();
	if (!pool) {
		lua_pushnil(L);
		lua_pushstring(L, "MongoClientPool not available");
		return 2;
	}
	lua_pushlightuserdata(L, static_cast<void*>(pool));
	return 1;
}

// db_get_thread_info — expose DBThread basic info to Lua
//
// Returns a table: { index = <int>, running = <bool>, healthy = <bool> }
// Useful for Lua scripts that need to identify which thread they're running on
// or check the thread's health status for diagnostic purposes.
//
// Captures DBScriptVM* as upvalue(1), reads DBThread* from CustomPtr slot.

int l_db_get_thread_info(lua_State* L) {
	auto& vm = *static_cast<DBScriptVM*>(lua_touserdata(L, lua_upvalueindex(1)));
	auto* thread = vm.GetDBThread();
	if (!thread) {
		lua_pushnil(L);
		lua_pushstring(L, "DBThread not available");
		return 2;
	}

	lua_newtable(L);
	lua_pushinteger(L, thread->Index());
	lua_setfield(L, -2, "index");
	lua_pushboolean(L, thread->IsRunning() ? 1 : 0);
	lua_setfield(L, -2, "running");
	lua_pushboolean(L, thread->IsHealthy() ? 1 : 0);
	lua_setfield(L, -2, "healthy");
	return 1;
}

// l_push_kv — helper: push key + value and set into table at -3

inline void l_push_kv(lua_State* L, const char* key, const char* val) {
	lua_pushstring(L, key);
	lua_pushstring(L, val);
	lua_settable(L, -3);
}
inline void l_push_kv(lua_State* L, const char* key, int val) {
	lua_pushstring(L, key);
	lua_pushinteger(L, val);
	lua_settable(L, -3);
}
inline void l_push_kv(lua_State* L, const char* key, bool val) {
	lua_pushstring(L, key);
	lua_pushboolean(L, val ? 1 : 0);
	lua_settable(L, -3);
}

// db_get_config — expose DbServiceConfig to Lua as a nested table
//
// Returns a nested table with the full DbServiceConfig tree:
//   {
//     log = { dir, level, rotation_size_mb, max_backup_files },
//     thread_pool = { thread_count, request_queue_size, response_queue_size,
//                     target_fps, max_requests_per_frame },
//     connection_pool = { max_pool_size, wait_queue_timeout_ms },
//     script = { runtime_scripts_dir, db_scripts_dir, auto_load }
//   }
//
// Captures DBScriptVM* as upvalue(1), reads DBThread* → GetConfig().
// The config is immutable after Start(), so no thread-safety concern.

int l_db_get_config(lua_State* L) {
	auto& vm = *static_cast<DBScriptVM*>(lua_touserdata(L, lua_upvalueindex(1)));
	auto* thread = vm.GetDBThread();
	if (!thread) {
		lua_pushnil(L);
		lua_pushstring(L, "DBThread not available");
		return 2;
	}
	const auto& cfg = thread->GetConfig();

	lua_newtable(L);  // root table

	// cfg.log
	lua_newtable(L);
	l_push_kv(L, "dir", cfg.log.dir.c_str());
	l_push_kv(L, "level", cfg.log.level.c_str());
	l_push_kv(L, "rotation_size_mb", cfg.log.rotation_size_mb);
	l_push_kv(L, "max_backup_files", cfg.log.max_backup_files);
	lua_setfield(L, -2, "log");

	// cfg.thread_pool
	lua_newtable(L);
	l_push_kv(L, "thread_count", cfg.thread_pool.thread_count);
	l_push_kv(L, "request_queue_size", cfg.thread_pool.request_queue_size);
	l_push_kv(L, "response_queue_size", cfg.thread_pool.response_queue_size);
	l_push_kv(L, "target_fps", cfg.thread_pool.target_fps);
	l_push_kv(L, "max_requests_per_frame", cfg.thread_pool.max_requests_per_frame);
	lua_setfield(L, -2, "thread_pool");

	// cfg.connection_pool
	lua_newtable(L);
	l_push_kv(L, "max_pool_size", cfg.connection_pool.max_pool_size);
	l_push_kv(L, "wait_queue_timeout_ms", cfg.connection_pool.wait_queue_timeout_ms);
	lua_setfield(L, -2, "connection_pool");

	// cfg.script
	lua_newtable(L);
	l_push_kv(L, "runtime_scripts_dir", cfg.script.runtime_scripts_dir.c_str());
	l_push_kv(L, "db_scripts_dir", cfg.script.db_scripts_dir.c_str());
	l_push_kv(L, "auto_load", cfg.script.auto_load);
	lua_setfield(L, -2, "script");

	return 1;
}

}  // namespace

// ExportDbLog — register per-thread log functions bound to a Quill logger
//
// Implementation note (upvalue reuse pattern):
//   Push the logger pointer once, then use lua_pushvalue to copy it for each
//   closure. This avoids duplicating the lightuserdata on the stack. The last
//   lua_pushcclosure consumes the original — no manual pop needed.
//
// After this call, Lua scripts inside this DBThread's DBScriptVM can write:
//   log_info("player login: uid=" .. uid)
//   log_error("query failed: " .. err)
//
// Output lands in logs/db_service/db_vm_{N}_<timestamp>.log via the per-thread
// Quill logger (R9).

void ExportDbLog(ScriptVM& vm, quill::Logger* logger) {
	auto L = vm.GetState();

	lua_pushlightuserdata(L, logger);

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_log_trace, 1);
	lua_setglobal(L, "log_trace");
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_log_debug, 1);
	lua_setglobal(L, "log_debug");
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_log_info, 1);
	lua_setglobal(L, "log_info");
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_log_warn, 1);
	lua_setglobal(L, "log_warn");
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_log_error, 1);
	lua_setglobal(L, "log_error");
	lua_pushcclosure(L, l_db_log_fatal, 1);
	lua_setglobal(L, "log_fatal");
}

// ExportDbRuntime — register db_* runtime globals
//
// Registers global functions that Lua scripts use to obtain:
//   db_get_client()      — MongoClient* (lightuserdata, for mongoc.* APIs)
//   db_get_pool()        — MongoClientPool* (lightuserdata, for pool access)
//   db_get_thread_info() — {index, running, healthy} table
//   db_get_config()      — {log, thread_pool, connection_pool, script} nested table
//
// All four capture the DBScriptVM* as an upvalue so they can read from the
// CustomPtrStore at call time. The store is re-populated every EventLoop
// restart, so pointers are always fresh.
//
// Called after ExportMongo and before InitScript in the EventLoop init sequence.

void ExportDbRuntime(ScriptVM& vm) {
	auto L = vm.GetState();

	ExportDbBsonCodec(vm);

	lua_pushlightuserdata(L, &vm);

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_get_client, 1);
	lua_setglobal(L, "db_get_client");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_get_pool, 1);
	lua_setglobal(L, "db_get_pool");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_get_thread_info, 1);
	lua_setglobal(L, "db_get_thread_info");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, l_db_get_config, 1);
	lua_setglobal(L, "db_get_config");

	lua_pop(L, 1);
}

// DBScriptVM member functions

DBScriptVM::DBScriptVM() = default;

DBScriptVM::~DBScriptVM() = default;

void DBScriptVM::RegisterSubsystemObjects(DBThread* thread,
										  mongo::MongoClient* client,
										  mongo::MongoClientPool* pool) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));

	// Reserve slots 1..4 in the CustomPtrStore.
	// Must match the maximum value in DbCustomPtr enum.
	store.Reserve(kDbPtrPool);

	store.Set(kDbPtrDBThread, thread);
	store.Set(kDbPtrScriptVM, this);
	store.Set(kDbPtrClient, client);
	store.Set(kDbPtrPool, pool);
}

DBThread* DBScriptVM::GetDBThread() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<DBThread>(kDbPtrDBThread);
}

mongo::MongoClient* DBScriptVM::GetMongoClient() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<mongo::MongoClient>(kDbPtrClient);
}

mongo::MongoClientPool* DBScriptVM::GetMongoClientPool() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<mongo::MongoClientPool>(kDbPtrPool);
}

bool DBScriptVM::AreCoreSlotsValid() const {
	return GetDBThread() != nullptr && GetMongoClient() != nullptr &&
		   GetMongoClientPool() != nullptr && GetCustomPtr(kDbPtrScriptVM) != nullptr;
}

// CallFrameCallback — invoke Lua global on_db_frame(info) once per frame
//
// Looks up the global function on_db_frame in the VM's Lua state.
// If defined: constructs an info table {frame_count, delta_seconds} and
// calls on_db_frame(info) via lua_pcall. Lua errors are caught and logged
// through the DBThread's logger (obtained via CustomPtrStore).
// If not defined: silently returns (not every script needs a frame callback).
//
// Called from DBThread::EventLoop after request processing and before
// frame-rate sleep. DBT-exclusive — no thread-safety concern.

void DBScriptVM::CallFrameCallback(int64_t frame_count, double delta_seconds) {
	auto L = GetState();
	const int base_top = lua_gettop(L);

	lua_getglobal(L, "on_db_frame");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	lua_newtable(L);
	lua_pushinteger(L, frame_count);
	lua_setfield(L, -2, "frame_count");
	lua_pushnumber(L, delta_seconds);
	lua_setfield(L, -2, "delta_seconds");

	int msgh = PushLuaErrorHandlerForCall(L, 1);
	if (lua_pcall(L, 1, 0, msgh) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		auto* thread = GetDBThread();
		auto* logger = thread ? thread->GetLogger() : nullptr;
		if (logger) {
			ENGINE_LOG_ERROR(logger,
							 "DBThread[{}]: on_db_frame error: {}",
							 thread->Index(),
							 err ? err : "unknown");
		}
		lua_settop(L, base_top);
		return;
	}

	lua_settop(L, base_top);
}

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
