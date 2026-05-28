#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

ENGINE_API void ExportRpc(ScriptVM& vm);

}  // namespace script
}  // namespace engine
