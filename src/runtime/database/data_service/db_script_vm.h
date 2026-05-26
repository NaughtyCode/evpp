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

enum DbCustomPtr : int {
    kDbPtrDBThread  = 1,
    kDbPtrScriptVM  = 2,
    kDbPtrClient    = 3,
    kDbPtrPool      = 4,
};

class DBScriptVM : public ScriptVM {
public:
    DBScriptVM();
    ~DBScriptVM() override;

    DBScriptVM(const DBScriptVM&) = delete;
    DBScriptVM& operator=(const DBScriptVM&) = delete;

    void RegisterSubsystemObjects(DBThread* thread,
                                  mongo::MongoClient* client,
                                  mongo::MongoClientPool* pool);

    DBThread*               GetDBThread() const;
    mongo::MongoClient*     GetMongoClient() const;
    mongo::MongoClientPool* GetMongoClientPool() const;

    bool AreCoreSlotsValid() const;
};

// Register per-thread log functions bound to a DBThread-specific Quill logger.
void ExportDbLog(ScriptVM& vm, quill::Logger* logger);

// Register db_get_client / db_get_pool global functions for Lua scripts.
void ExportDbRuntime(ScriptVM& vm);

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
