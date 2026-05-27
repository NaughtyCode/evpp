#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Register mongoc and bson Lua modules into the given VM.
// Creates two global module tables: "mongoc" and "bson".
ENGINE_API void ExportMongo(ScriptVM& vm);

}  // namespace script
}  // namespace engine

#endif
