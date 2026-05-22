#pragma once

namespace engine {

class ScriptVM;

namespace script {

// Main entry point — call once after VM is created to export all C++ APIs
// to the Lua environment. Each sub-module registers its own set of
// functions / modules.
void ExportAll(ScriptVM& vm);

// Per-module entry points (also callable individually)
void ExportLog(ScriptVM& vm);
void ExportTimer(ScriptVM& vm);

// Shutdown: cancel all Lua-owned timers, release Lua references.
// Call before destroying the ScriptVM.
void ShutdownTimerBindings();

} // namespace script
} // namespace engine
