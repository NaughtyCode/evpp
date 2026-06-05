#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

/* Lua sandbox security levels.
 *
 * Controls which standard libraries are loaded and which dangerous
 * functions are removed when a ScriptVM is created.
 *
 *   Strict  - Production: no io, no os.execute/exit/remove/rename/getenv/tmpname,
 *             no debug, native package loading disabled.
 *   Server  - Trusted server: io and os allowed, native package loading disabled,
 *             debug removed.
 *   Full    - Development: all libraries loaded, native package loading disabled.
 */
enum class LuaSandboxLevel {
	Strict,  /* Production / untrusted scripts */
	Server,  /* Trusted server-side scripts */
	Full     /* Development only */
};

/* Sandboxed replacement for luaL_openlibs.
 *
 * Loads only whitelisted libraries based on the configured security level.
 * Always loads: base, table, string, math, utf8, coroutine, package.
 * Conditionally loads: io, os, debug based on level.
 * Native package loading is disabled for every level: package.loadlib is nil,
 * package.cpath is empty, and C/C-root searchers are removed.
 *
 * @param L      The Lua state to initialize.
 * @param level  The sandbox security level. */
CLOUD_ENGINE_API void luaL_openlibs_sandboxed(lua_State* L, LuaSandboxLevel level);

}  // namespace engine
