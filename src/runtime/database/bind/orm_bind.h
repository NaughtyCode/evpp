#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

CLOUD_ENGINE_API void ExportOrm(ScriptVM& vm);

}  // namespace script
}  // namespace engine
