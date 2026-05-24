#include "engine/script/script_bind.h"

#include "engine/core/log/log.h"
#include "engine/profiler/profiler_events.h"
#include "engine/script/log_bind.h"
#include "engine/script/timer_bind.h"
#include "engine/script/msgpack_bind.h"
#include "engine/script/net_bind.h"
#include "engine/script/import_bind.h"
#include "engine/vm/vm.h"

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

    ENGINE_LOG_INFO(logger, "ScriptBind: all APIs exported");
}

} // namespace script
} // namespace engine
