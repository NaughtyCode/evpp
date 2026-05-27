#include "runtime/vm/sandbox.h"

#include "runtime/core/log/log.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace engine {

void luaL_openlibs_sandboxed(lua_State* L, LuaSandboxLevel level) {
	auto* logger = GetLogger();

	/* ── Always safe — pure computation, no OS access ─────────────── */
	luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
	lua_pop(L, 1);

	/* ── package — needed for require/import, but remove loadlib ──── */
	luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
	/* Remove dangerous package.loadlib to prevent loading native C libraries */
	lua_getglobal(L, "package");
	lua_pushnil(L);
	lua_setfield(L, -2, "loadlib");
	lua_pop(L, 1);

	/* ── Level-specific libraries ─────────────────────────────────── */
	switch (level) {
	case LuaSandboxLevel::Strict: {
		/* No io library — prevents file system access */
		/* os library loaded but with dangerous functions removed */
		luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
		lua_pop(L, 1);
		lua_getglobal(L, "os");
		lua_pushnil(L); lua_setfield(L, -2, "execute");
		lua_pushnil(L); lua_setfield(L, -2, "exit");
		lua_pushnil(L); lua_setfield(L, -2, "remove");
		lua_pushnil(L); lua_setfield(L, -2, "rename");
		lua_pushnil(L); lua_setfield(L, -2, "getenv");
		lua_pop(L, 1);
		/* No debug library */
		break;
	}
	case LuaSandboxLevel::Server: {
		/* io and os allowed (trusted server scripts) */
		luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
		lua_pop(L, 1);
		/* debug is not loaded — still too dangerous */
		/* package.loadlib already removed above */
		break;
	}
	case LuaSandboxLevel::Full: {
		/* All libraries for development */
		luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_DBLIBNAME, luaopen_debug, 1);
		lua_pop(L, 1);
		/* package.loadlib already removed above */
		break;
	}
	}

	/* ── base library — always loaded last (contains print, error, pcall, etc.) ── */
	luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
	lua_pop(L, 1);

	const char* level_str = "unknown";
	switch (level) {
	case LuaSandboxLevel::Strict:  level_str = "strict";  break;
	case LuaSandboxLevel::Server:  level_str = "server";  break;
	case LuaSandboxLevel::Full:    level_str = "full";    break;
	}
	ENGINE_LOG_INFO(logger,
					"ScriptVM: sandbox initialized, level=[{}], "
					"base/table/string/math/utf8/coroutine/package loaded",
					level_str);
}

}  // namespace engine
