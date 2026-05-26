#pragma once

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error "db_script_vm.h is internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including this header."
#endif

#if defined(ENGINE_MONGODB_ENABLED)

#include <cstdint>

#include <quill/Logger.h>

#include "runtime/vm/vm.h"

namespace engine {

class DBThread;
namespace mongo {
class MongoClient;
class MongoClientPool;
}

// ══════════════════════════════════════════════════════════════════════════════
// DbCustomPtr — indices into the per-VM CustomPtrStore
// ══════════════════════════════════════════════════════════════════════════════
//
// These slots are populated by DBScriptVM::RegisterSubsystemObjects() during
// EventLoop initialisation (R5). Lua scripts access the stored C++ pointers
// indirectly through global functions registered by ExportDbRuntime:
//   db_get_client() → kDbPtrClient   (MongoClient*, as lightuserdata)
//   db_get_pool()   → kDbPtrPool     (MongoClientPool*, as lightuserdata)
//
// Slots 1-4 are reserved. Expanding this enum requires updating the
// Reserve() call in RegisterSubsystemObjects().

enum DbCustomPtr : int {
    kDbPtrDBThread  = 1,  // DBThread*        — owning thread
    kDbPtrScriptVM  = 2,  // DBScriptVM*      — self-reference for upvalue access
    kDbPtrClient    = 3,  // MongoClient*     — per-thread exclusive client (from pool)
    kDbPtrPool      = 4,  // MongoClientPool* — shared pool (for scripts that need pool ops)
};

// ══════════════════════════════════════════════════════════════════════════════
// DBScriptVM — per-DBThread Lua VM (subclass of ScriptVM)
// ══════════════════════════════════════════════════════════════════════════════
//
// Each DBThread owns one DBScriptVM. It reuses ScriptVM's infrastructure
// (Lua state lifecycle, ScriptImporter, Create/DestroyScript, DoString,
// DoDirectory, InitScript) and adds DB-specific bindings.
//
// Exclusively accessed by the owning DBThread inside EventLoop (R6 — module
// external code never touches this class). Thread safety is guaranteed by
// thread confinement, not by internal locks.
//
// Binding sequence in EventLoop (see DBThread::EventLoop):
//   1. RegisterSubsystemObjects() — store DBThread/MongoClient/MongoClientPool ptrs
//   2. ExportDbLog()               — per-thread log functions (R9)
//   3. script::ExportMongo()       — mongoc.* / bson.* global modules (R10)
//   4. ExportDbRuntime()           — db_get_client / db_get_pool globals
//   5. SetImportPath()             — configure require() search order (R12)
//   6. DoDirectory() + InitScript()— load scripts (R12)

class DBScriptVM : public ScriptVM {
public:
    DBScriptVM();
    ~DBScriptVM() override;

    DBScriptVM(const DBScriptVM&) = delete;
    DBScriptVM& operator=(const DBScriptVM&) = delete;

    // Register subsystem object pointers into the VM's CustomPtrStore.
    // Must be called once at EventLoop start, before any script execution.
    // Internally reserves 4 slots via VMCustomPtrStore::Reserve(kDbPtrPool).
    void RegisterSubsystemObjects(DBThread* thread,
                                  mongo::MongoClient* client,
                                  mongo::MongoClientPool* pool);

    // Typed accessors for subsystem objects (read from CustomPtrStore).
    // All are DBT-exclusive — the owning DBThread calls them.
    DBThread*               GetDBThread() const;
    mongo::MongoClient*     GetMongoClient() const;
    mongo::MongoClientPool* GetMongoClientPool() const;

    // Returns true if all four core slots are non-null.
    // Used as a sanity check after RegisterSubsystemObjects().
    bool AreCoreSlotsValid() const;
};

// ══════════════════════════════════════════════════════════════════════════════
// ExportDbLog — register per-DBThread log functions (R9)
// ══════════════════════════════════════════════════════════════════════════════
//
// Registers log_trace / log_debug / log_info / log_warn / log_error / log_fatal
// as Lua globals. Each function captures the DBThread's Quill logger pointer
// as an upvalue (lightuserdata), so Lua-side log calls write to the correct
// per-thread log file (logs/db_service/db_vm_{N}.log).
//
// This replaces the default script::ExportLog which binds to the global "root"
// logger. DB service scripts must NOT share the root logger (R9).
//
// Called during EventLoop initialisation (after logger creation, before
// script loading).

void ExportDbLog(ScriptVM& vm, quill::Logger* logger);

// ══════════════════════════════════════════════════════════════════════════════
// ExportDbRuntime — register db_get_client / db_get_pool globals (R5)
// ══════════════════════════════════════════════════════════════════════════════
//
// These globals let Lua scripts retrieve the current thread's MongoClient*
// and pool's MongoClientPool* from the VM's CustomPtrStore (through the
// DBScriptVM* upvalue). Used together with ExportMongo's mongoc.* bindings:
//
//   local mongoc = require("mongoc")
//   local client = db_get_client()
//   local coll = mongoc.collection.new(client, "db", "coll")
//   coll:insert_one(doc)
//
// Called after ExportMongo (R10) and before InitScript.

void ExportDbRuntime(ScriptVM& vm);

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
