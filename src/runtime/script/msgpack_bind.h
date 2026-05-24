#pragma once

#include "runtime/core/engine_export.h"

namespace engine {

class ScriptVM;

namespace script {

// Export the "cmsgpack" and "cmsgpack_safe" modules to Lua.
//
//   cmsgpack.pack(...)          → string
//   cmsgpack.unpack(str)         → ... values
//   cmsgpack.unpack_one(str [, offset]) → value, next_offset
//   cmsgpack.unpack_limit(str, limit [, offset]) → values..., next_offset
//
// The "cmsgpack_safe" module has the same API but wraps every call in
// pcall: on error it returns (nil, errmsg) instead of raising.
ENGINE_API void ExportMsgPack(ScriptVM& vm);

} // namespace script
} // namespace engine
