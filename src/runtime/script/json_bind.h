#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Export the "json" and "json_safe" modules to Lua.
//
// Core APIs:
//   json.decode(text [, opts]) / json.parse(text [, opts]) -> value
//   json.encode(value [, opts]) / json.stringify(value [, opts]) -> string
//   json.load(path [, opts]) / json.read_file(path [, opts]) -> value
//   json.save(path, value [, opts]) / json.write_file(path, value [, opts]) -> true
//   json.validate(text [, opts]) -> true | false, errmsg
//   json.minify(text [, opts]) -> string
//   json.prettify(text [, opts]) -> string
//
// Helpers:
//   json.null, json.array(...), json.object([table])
//   json.as_array(table), json.as_object(table)
//   json.is_array(value), json.is_object(value), json.is_null(value), json.type(value)
//
// opts.comments enables JSONC parsing/validation/formatting. opts.pretty or
// opts.prettify enables pretty output for encode/save.
//
// The "json_safe" module has the same API but wraps every call in pcall:
// on error it returns (nil, errmsg) instead of raising.
CLOUD_ENGINE_API void ExportJson(ScriptVM& vm);

}  // namespace script
}  // namespace engine
