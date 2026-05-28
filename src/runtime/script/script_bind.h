#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Main entry point — call once after VM is created to export all C++ APIs
// to the Lua environment. Each sub-module registers its own set of
// functions / modules.
ENGINE_API void ExportAll(ScriptVM& vm, TimerManager& tm);

// Per-module entry points (also callable individually)
ENGINE_API void ExportLog(ScriptVM& vm);
ENGINE_API void ExportTimer(ScriptVM& vm, TimerManager& tm);
ENGINE_API void ExportNet(ScriptVM& vm);
ENGINE_API void ExportMsgPack(ScriptVM& vm);
ENGINE_API void ExportMongo(ScriptVM& vm);
ENGINE_API void ExportDbService(ScriptVM& vm);

// Shutdown: cancel all Lua-owned objects, release Lua references.
// Call before destroying the ScriptVM.
ENGINE_API void ShutdownTimerBindings(ScriptVM& vm);
ENGINE_API void ShutdownNetBindings();

ENGINE_API void ExportEntity(ScriptVM& vm);
ENGINE_API void ShutdownEntityBindings();

ENGINE_API void ExportSpace(ScriptVM& vm);

ENGINE_API void ExportAOI(ScriptVM& vm);
#if defined(ENGINE_MONGODB_ENABLED)
ENGINE_API void ExportOrm(ScriptVM& vm);
#endif
ENGINE_API void ExportRpc(ScriptVM& vm);
ENGINE_API void UpdateRpcBindings(ScriptVM& vm);
ENGINE_API void ShutdownRpcBindings(ScriptVM& vm);
ENGINE_API void ExportAuth(ScriptVM& vm);
ENGINE_API void ExportMem(ScriptVM& vm);

}  // namespace script
}  // namespace engine
