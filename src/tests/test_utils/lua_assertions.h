#pragma once

#include <string>

#include <catch2/catch_test_macros.hpp>

struct lua_State;

namespace test {

// ── Lua state inspection helpers ───────────────────────────────────────

// Assert that the Lua stack size equals `expected`.
inline void ExpectLuaStackSize(lua_State* L, int expected) {
	int top = lua_gettop(L);
	INFO("Expected Lua stack size " << expected << " but got " << top);
	REQUIRE(top == expected);
}

// Assert that the Lua stack is clean (size 0).
inline void ExpectLuaStackClean(lua_State* L) {
	int top = lua_gettop(L);
	INFO("Expected clean Lua stack but got " << top << " element(s)");
	REQUIRE(top == 0);
}

// Assert that a Lua global variable has the expected string value.
inline void ExpectLuaGlobalString(lua_State* L, const std::string& name,
								  const std::string& expected) {
	lua_getglobal(L, name.c_str());
	REQUIRE(lua_isstring(L, -1));
	std::string actual = lua_tostring(L, -1);
	lua_pop(L, 1);
	INFO("Lua global '" << name << "': expected '" << expected << "', got '" << actual << "'");
	REQUIRE(actual == expected);
}

// Assert that a Lua global variable has the expected integer value.
inline void ExpectLuaGlobalInt(lua_State* L, const std::string& name, int expected) {
	lua_getglobal(L, name.c_str());
	REQUIRE(lua_isinteger(L, -1));
	int actual = static_cast<int>(lua_tointeger(L, -1));
	lua_pop(L, 1);
	INFO("Lua global '" << name << "': expected " << expected << ", got " << actual);
	REQUIRE(actual == expected);
}

// Assert that a Lua global is nil (does not exist).
inline void ExpectLuaGlobalNil(lua_State* L, const std::string& name) {
	lua_getglobal(L, name.c_str());
	bool is_nil = lua_isnil(L, -1);
	lua_pop(L, 1);
	INFO("Lua global '" << name << "' should be nil but is not");
	REQUIRE(is_nil);
}

// Assert that a Lua global is a table.
inline void ExpectLuaGlobalTable(lua_State* L, const std::string& name) {
	lua_getglobal(L, name.c_str());
	bool is_table = lua_istable(L, -1);
	lua_pop(L, 1);
	INFO("Lua global '" << name << "' should be a table but is not");
	REQUIRE(is_table);
}

}  // namespace test
