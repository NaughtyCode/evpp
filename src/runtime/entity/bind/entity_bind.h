#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
class ScriptVM;

namespace script {

// Register the entity metatable and export the "entity" module to Lua.
CLOUD_ENGINE_API void ExportEntity(ScriptVM& vm);

// Release all Lua-owned entity resources. Call before destroying ScriptVM.
CLOUD_ENGINE_API void ShutdownEntityBindings();

}  // namespace script
}  // namespace engine
