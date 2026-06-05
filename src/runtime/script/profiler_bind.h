#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class MainThreadScriptVM;

namespace script {

// Export the "profiler" Lua module. Only the main-thread Lua VM type owns
// this binding surface.
CLOUD_ENGINE_API bool ExportProfiler(MainThreadScriptVM& vm);
CLOUD_ENGINE_API void ShutdownProfilerBindings(MainThreadScriptVM& vm);

}  // namespace script
}  // namespace engine
