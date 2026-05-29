#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

/* Lua sandbox security levels.
 *
 * Controls which standard libraries are loaded and which dangerous
 * functions are removed when a ScriptVM is created.
 *
 *   Strict  — Production: no io, no os.execute/exit/remove/rename/getenv,
 *             no debug, no package.loadlib.
 *   Server  — Trusted server: io and os allowed, package.loadlib removed,
 *             debug removed.
 *   Full    — Development: all libraries loaded, only package.loadlib removed.
 */
enum class LuaSandboxLevel {
	Strict,  /* Production / untrusted scripts */
	Server,  /* Trusted server-side scripts */
	Full     /* Development only — equivalent to luaL_openlibs */
};

/* Sandboxed replacement for luaL_openlibs.
 *
 * Loads only whitelisted libraries based on the configured security level.
 * Always loads: base, table, string, math, utf8, coroutine, package (no loadlib).
 * Conditionally loads: io, os, debug based on level.
 * Never loads: package.loadlib (replaced with nil).
 *
 * @param L      The Lua state to initialize.
 * @param level  The sandbox security level. */
CLOUD_ENGINE_API void luaL_openlibs_sandboxed(lua_State* L, LuaSandboxLevel level);

}  // namespace engine
