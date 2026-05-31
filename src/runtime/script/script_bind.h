#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

class ScriptVM;
class TimerManager;

namespace space {
class Space;
}  // namespace space

namespace script {

// Main entry point — call once after VM is created to export all C++ APIs
// to the Lua environment. Each sub-module registers its own set of
// functions / modules.
CLOUD_ENGINE_API void ExportAll(ScriptVM& vm, TimerManager& tm);

// Per-module entry points (also callable individually)
CLOUD_ENGINE_API void ExportLog(ScriptVM& vm);
CLOUD_ENGINE_API void ExportTimer(ScriptVM& vm, TimerManager& tm);
CLOUD_ENGINE_API void ExportNet(ScriptVM& vm);
CLOUD_ENGINE_API void ExportMsgPack(ScriptVM& vm);
CLOUD_ENGINE_API void ExportJson(ScriptVM& vm);
CLOUD_ENGINE_API void ExportMongo(ScriptVM& vm);
CLOUD_ENGINE_API void ExportDbService(ScriptVM& vm);

// Shutdown: cancel all Lua-owned objects, release Lua references.
// Call before destroying the ScriptVM.
CLOUD_ENGINE_API void ShutdownTimerBindings(ScriptVM& vm);
CLOUD_ENGINE_API void ShutdownNetBindings();
CLOUD_ENGINE_API void ShutdownConfigBindings(ScriptVM& vm);
CLOUD_ENGINE_API int FlushConfigCallbacks(::lua_State* L);

CLOUD_ENGINE_API void ExportEntity(ScriptVM& vm);
CLOUD_ENGINE_API void ShutdownEntityBindings();

CLOUD_ENGINE_API void ExportSpace(ScriptVM& vm, space::Space* current_space);

CLOUD_ENGINE_API void ExportAOI(ScriptVM& vm);
#if defined(ENGINE_MONGODB_ENABLED)
CLOUD_ENGINE_API void ExportOrm(ScriptVM& vm);
#endif
CLOUD_ENGINE_API void ExportRpc(ScriptVM& vm);
CLOUD_ENGINE_API void UpdateRpcBindings(ScriptVM& vm);
CLOUD_ENGINE_API void ShutdownRpcBindings(ScriptVM& vm);
CLOUD_ENGINE_API void ExportAuth(ScriptVM& vm);
CLOUD_ENGINE_API void ExportMem(ScriptVM& vm);
CLOUD_ENGINE_API void ExportConfigBindings(ScriptVM& vm);

}  // namespace script
}  // namespace engine
