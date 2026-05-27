#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include <cstdint>
#include <new>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

// ── Userdata helpers ────────────────────────────────────────────────────

template <typename T>
T** CheckUserdata(lua_State* L, int idx, const char* meta_name) {
	return static_cast<T**>(luaL_checkudata(L, idx, meta_name));
}

template <typename T>
T* GetUserdata(lua_State* L, int idx, const char* meta_name) {
	return *CheckUserdata<T>(L, idx, meta_name);
}

template <typename T>
T** NewUserdata(lua_State* L, const char* meta_name) {
	auto* ud = static_cast<T**>(lua_newuserdata(L, sizeof(T*)));
	*ud = nullptr;
	luaL_setmetatable(L, meta_name);
	return ud;
}

// ── Metatable registration ──────────────────────────────────────────────

inline void RegisterMetatable(lua_State* L,
							  const char* name,
							  const luaL_Reg* funcs,
							  lua_CFunction gc_func) {
	luaL_newmetatable(L, name);
	if (funcs) {
		luaL_setfuncs(L, funcs, 0);
	}
	lua_pushcfunction(L, gc_func);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
}

// ── Module helpers ──────────────────────────────────────────────────────

// Start building a named module table. Leaves the new table on the stack.
inline void BeginModule(lua_State* L) {
	lua_newtable(L);
}

// Add a luaL_Reg array to the module table at stack top.
inline void AddToModule(lua_State* L, const luaL_Reg* funcs) {
	luaL_setfuncs(L, funcs, 0);
}

// Finish the module: set it as a global and pop it.
inline void EndModule(lua_State* L, const char* name) {
	lua_pushvalue(L, -1);
	lua_setglobal(L, name);
	lua_pop(L, 1);
}

}  // namespace script
}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
