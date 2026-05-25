#include "runtime/script/script_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/log_bind.h"
#include "runtime/script/timer_bind.h"
#include "runtime/script/msgpack_bind.h"
#include "runtime/script/net_bind.h"
#include "runtime/script/import_bind.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ExportAll(ScriptVM& vm) {
    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: exporting all APIs to Lua...");

    { ENGINE_PROFILE_SCRIPT_EXPORT("log");    ExportLog(vm);    }
    { ENGINE_PROFILE_SCRIPT_EXPORT("timer");  ExportTimer(vm);  }
    { ENGINE_PROFILE_SCRIPT_EXPORT("net");    ExportNet(vm);    }
    { ENGINE_PROFILE_SCRIPT_EXPORT("msgpack"); ExportMsgPack(vm); }
    { ENGINE_PROFILE_SCRIPT_EXPORT("import"); engine::ExportImport(vm); }
    { ENGINE_PROFILE_SCRIPT_EXPORT("mongo");  ExportMongo(vm);  }

    ENGINE_LOG_INFO(logger, "ScriptBind: all APIs exported");
}

} // namespace script
} // namespace engine
