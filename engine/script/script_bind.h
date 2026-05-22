#pragma once

#include "engine/engine_export.h"

namespace engine {

class ScriptVM;

namespace script {

// Main entry point — call once after VM is created to export all C++ APIs
// to the Lua environment. Each sub-module registers its own set of
// functions / modules.
ENGINE_API void ExportAll(ScriptVM& vm);

// Per-module entry points (also callable individually)
ENGINE_API void ExportLog(ScriptVM& vm);
ENGINE_API void ExportTimer(ScriptVM& vm);
ENGINE_API void ExportNet(ScriptVM& vm);
ENGINE_API void ExportMsgPack(ScriptVM& vm);

// Shutdown: cancel all Lua-owned objects, release Lua references.
// Call before destroying the ScriptVM.
ENGINE_API void ShutdownTimerBindings();
ENGINE_API void ShutdownNetBindings();

} // namespace script
} // namespace engine
