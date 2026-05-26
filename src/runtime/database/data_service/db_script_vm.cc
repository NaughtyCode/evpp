#if defined(ENGINE_MONGODB_ENABLED)

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/db_script_vm.h"

#include "runtime/core/log/log.h"
#include "runtime/database/data_service/db_thread.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
#include "runtime/vm/custom_ptr_store.h"

namespace engine {

// ============================================================================
// Internal helpers (anonymous namespace)
// ============================================================================

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
    lua_pushlightuserdata(L, static_cast<void*>(pool));
    return 1;
}

} // namespace

// ============================================================================
// ExportDbLog — register per-thread log functions bound to a Quill logger
// ============================================================================

void ExportDbLog(ScriptVM& vm, quill::Logger* logger) {
    auto L = vm.GetState();

    lua_pushlightuserdata(L, logger);

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_trace, 1); lua_setglobal(L, "log_trace");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_debug, 1); lua_setglobal(L, "log_debug");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_info,  1); lua_setglobal(L, "log_info");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_warn,  1); lua_setglobal(L, "log_warn");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_error, 1); lua_setglobal(L, "log_error");
    lua_pushcclosure(L, l_db_log_fatal, 1);                     lua_setglobal(L, "log_fatal");
}

// ============================================================================
// ExportDbRuntime — register db_get_client / db_get_pool
// ============================================================================

void ExportDbRuntime(ScriptVM& vm) {
    auto L = vm.GetState();
    lua_pushlightuserdata(L, &vm);

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_get_client, 1);
    lua_setglobal(L, "db_get_client");

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_get_pool, 1);
    lua_setglobal(L, "db_get_pool");

    lua_pop(L, 1);
}

// ============================================================================
// DBScriptVM
// ============================================================================

DBScriptVM::DBScriptVM() = default;

DBScriptVM::~DBScriptVM() = default;

void DBScriptVM::RegisterSubsystemObjects(DBThread* thread,
                                          mongo::MongoClient* client,
                                          mongo::MongoClientPool* pool) {
    VMCustomPtrStore store(const_cast<lua_State*>(GetState()));

    store.Reserve(kDbPtrPool);

    store.Set(kDbPtrDBThread,  thread);
    store.Set(kDbPtrScriptVM,  this);
    store.Set(kDbPtrClient,    client);
    store.Set(kDbPtrPool,      pool);
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
    return GetDBThread() != nullptr &&
           GetMongoClient() != nullptr &&
           GetMongoClientPool() != nullptr &&
           GetCustomPtr(kDbPtrScriptVM) != nullptr;
}

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
