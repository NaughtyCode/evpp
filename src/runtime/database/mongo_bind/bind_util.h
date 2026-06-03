#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

#include "runtime/core/mem/mem.h"

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

template <typename T>
T CheckIntegerValue(lua_State* L, int idx, lua_Integer value) {
	static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
				  "T must be a non-bool integral type");

	if constexpr (std::is_signed_v<T>) {
		constexpr lua_Integer min_value = static_cast<lua_Integer>((std::numeric_limits<T>::min)());
		constexpr lua_Integer max_value = static_cast<lua_Integer>((std::numeric_limits<T>::max)());
		if (value < min_value || value > max_value) {
			luaL_argerror(L, idx, "integer out of range");
			return T{};
		}
	} else {
		if (value < 0) {
			luaL_argerror(L, idx, "integer out of range");
			return T{};
		}
		using UnsignedLuaInteger = std::make_unsigned_t<lua_Integer>;
		const auto unsigned_value = static_cast<UnsignedLuaInteger>(value);
		constexpr auto target_max = (std::numeric_limits<T>::max)();
		constexpr auto lua_max = (std::numeric_limits<UnsignedLuaInteger>::max)();
		if constexpr (target_max < lua_max) {
			if (unsigned_value > static_cast<UnsignedLuaInteger>(target_max)) {
				luaL_argerror(L, idx, "integer out of range");
				return T{};
			}
		}
	}

	return static_cast<T>(value);
}

template <typename T>
T CheckIntegerArg(lua_State* L, int idx) {
	return CheckIntegerValue<T>(L, idx, luaL_checkinteger(L, idx));
}

template <typename T>
T CheckLengthArg(lua_State* L, int idx) {
	return CheckIntegerValue<T>(L, idx, luaL_len(L, idx));
}

template <typename T>
T OptIntegerArg(lua_State* L, int idx, T default_value) {
	if (lua_isnoneornil(L, idx)) {
		return default_value;
	}
	return CheckIntegerArg<T>(L, idx);
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
