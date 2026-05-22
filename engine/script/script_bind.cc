#include "engine/script/script_bind.h"

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/script/log_bind.h"
#include "engine/script/timer_bind.h"
#include "engine/script/net_bind.h"
#include "engine/vm/vm.h"

namespace engine {
namespace script {

void ExportAll(ScriptVM& vm) {
    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: exporting all APIs to Lua...");

    ExportLog(vm);
    ExportTimer(vm);
    ExportNet(vm);

    ENGINE_LOG_INFO(logger, "ScriptBind: all APIs exported");
}

} // namespace script
} // namespace engine
