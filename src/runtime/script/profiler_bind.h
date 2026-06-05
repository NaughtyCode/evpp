#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Export the "profiler" Lua module. This function refuses to export unless
// the VM has been marked by ExportAll as the main-thread Lua VM.
CLOUD_ENGINE_API bool ExportProfiler(ScriptVM& vm);
CLOUD_ENGINE_API void ShutdownProfilerBindings(ScriptVM& vm);

}  // namespace script
}  // namespace engine
