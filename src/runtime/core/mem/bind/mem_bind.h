#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Export mem.get_stats / mem.reset_stats / mem.dump_stats / mem.is_enabled
// as a "mem" Lua module.  Only compiled when ENGINE_MEM_STATS_ENABLED is
// defined — the caller guards with #ifdef.
CLOUD_ENGINE_API void ExportMem(ScriptVM& vm);

}  // namespace script
}  // namespace engine
