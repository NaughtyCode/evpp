#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

/*
 * LuaError — documented, auditable alternative to luaL_error.
 *
 * Formats fmt+args into a 512-byte stack buffer, pushes the string,
 * and calls lua_error. Like luaL_error, lua_error uses longjmp —
 * callers MUST ensure no RAII objects with non-trivial destructors
 * are alive on the stack.
 *
 * Pattern: wrap temporary RAII objects in a scope block that exits
 * before any error path:
 *
 *   Resource* raw = nullptr;
 *   {
 *       auto guard = std::make_unique<Resource>(...);
 *       std::string name = BuildName();
 *       if (!error) raw = guard.release();
 *   }  // guard and name destroyed here
 *   if (error) return LuaError(L, "resource init failed");
 *   use(raw);
 *   return 0;
 *
 * Messages longer than 511 bytes are truncated.
 */
ENGINE_API int LuaError(lua_State* L, const char* fmt, ...);

}  // namespace script
}  // namespace engine
