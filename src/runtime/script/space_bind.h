#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Export space management API to Lua:
//   space.create(name, config)  → space_id
//   space.destroy(space_id)
//   space.send(space_id, target_entity, payload)
//   space.current()              → {id, name, entity_count}
ENGINE_API void ExportSpace(ScriptVM& vm);

}  // namespace script
}  // namespace engine
