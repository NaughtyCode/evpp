#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Export the "config" Lua module with get/get_module/on_change functions.
// Registered as a global table so Lua can call config.get(path), etc.
ENGINE_API void ExportConfigBindings(ScriptVM& vm);

}  // namespace script
}  // namespace engine
