#pragma once

#include "runtime/core/engine_export.h"

namespace engine {

class ScriptVM;

namespace script {

// Export log_trace / log_debug / log_info / log_warn / log_error / log_fatal
// as global Lua functions. Each takes a single string argument.
ENGINE_API void ExportLog(ScriptVM& vm);

} // namespace script
} // namespace engine
