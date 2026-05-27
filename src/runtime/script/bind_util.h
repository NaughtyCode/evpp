#pragma once

#include "runtime/core/engine_api.h"

#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

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

//==============================================================================
// Template helpers — eliminate duplicated boilerplate across network bindings
//
// These replace the manual lightuserdata + _ctx + disposed + luaL_ref + __gc
// pattern that was copy-pasted across 6 network binding files.
//
// Usage per binding:
//   1. GetCtxFromTable<T>(L, idx)  — instead of hand-written GetXxxCtxFromTable
//   2. PushInstanceTable(L, ctx, meta) — instead of manual table creation
//   3. RegisterInstanceMeta(L, name, methods, gc) — for metatable registration
//   4. CallInstMethod / CallInstMethodStr — for Lua callback dispatch
//   5. PushLibrary(L, funcs) — instead of raw luaL_newlib
//==============================================================================

//------------------------------------------------------------------------------
// GetCtxFromTable<T> — extract typed context from _ctx lightuserdata field
//
// Replaces the 6 hand-written GetXxxCtxFromTable functions.
//------------------------------------------------------------------------------

template <typename T>
T* GetCtxFromTable(lua_State* L, int idx) {
	lua_getfield(L, idx, "_ctx");
	auto* ctx = static_cast<T*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return ctx;
}

//------------------------------------------------------------------------------
// PushInstanceTable — create Lua instance table with _ctx + metatable
//
// Replaces the 4-line pattern repeated in every connect/listen/new function.
//------------------------------------------------------------------------------

inline void PushInstanceTable(lua_State* L, void* ctx, const char* meta_name) {
	lua_newtable(L);
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, -2, "_ctx");
	luaL_getmetatable(L, meta_name);
	lua_setmetatable(L, -2);
}

//------------------------------------------------------------------------------
// RegisterInstanceMeta — register metatable with methods, __index, and __gc
//------------------------------------------------------------------------------

inline void RegisterInstanceMeta(lua_State* L, const char* name,
                                  const luaL_Reg* methods,
                                  lua_CFunction gc_func) {
	luaL_newmetatable(L, name);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	if (methods) {
		luaL_setfuncs(L, methods, 0);
	}
	lua_pushcfunction(L, gc_func);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
}

//------------------------------------------------------------------------------
// CallInstMethod — call a no-arg method on a Lua instance by registry ref
//------------------------------------------------------------------------------

inline void CallInstMethod(lua_State* L, int inst_ref, const char* method) {
	if (!L || inst_ref == LUA_NOREF) return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, method);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return;
	}
	lua_insert(L, -2);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		lua_pop(L, 1);
	}
}

//------------------------------------------------------------------------------
// CallInstMethodStr — call a one-string-arg method on a Lua instance by ref
//------------------------------------------------------------------------------

inline void CallInstMethodStr(lua_State* L, int inst_ref, const char* method,
                               const std::string& arg) {
	if (!L || inst_ref == LUA_NOREF) return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, method);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return;
	}
	lua_insert(L, -2);
	lua_pushlstring(L, arg.data(), arg.size());
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		lua_pop(L, 1);
	}
}

//------------------------------------------------------------------------------
// CallInstMethodTableStr — call method(table_ref, str) on Lua instance by ref
//
// Used for server callbacks: on_connect(self, conn_table, addr)
//------------------------------------------------------------------------------

inline void CallInstMethodTableStr(lua_State* L, int inst_ref, const char* method,
                                    int table_ref, const std::string& arg) {
	if (!L || inst_ref == LUA_NOREF) return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, inst_ref);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, method);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return;
	}
	lua_insert(L, -2);
	lua_rawgeti(L, LUA_REGISTRYINDEX, table_ref);
	lua_pushlstring(L, arg.data(), arg.size());
	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		lua_pop(L, 1);
	}
}

//------------------------------------------------------------------------------
// PushLibrary — create a library table from luaL_Reg array
//------------------------------------------------------------------------------

inline void PushLibrary(lua_State* L, const luaL_Reg* funcs) {
	luaL_newlib(L, funcs);
}

}  // namespace script
}  // namespace engine
