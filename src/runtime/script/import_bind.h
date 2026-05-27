#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

// Register the "import" Lua function (with sub-functions setpath/addpath/
// loaded/clearcache) into the VM. Uses the per-VM ScriptImporter created
// by ScriptVM::GetImporter().
ENGINE_API void ExportImport(ScriptVM& vm);

}  // namespace engine
