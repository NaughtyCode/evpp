#include "runtime/script/import_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/vm/script_importer.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

namespace {

ScriptImporter* GetImporter(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__ScriptImporter");
	auto* importer = static_cast<ScriptImporter*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return importer;
}

// ── Lua C functions ─────────────────────────────────────────────────────

int l_import(lua_State* L) {
	size_t len = 0;
	const char* name = luaL_checklstring(L, 1, &len);
	if (len == 0) {
		return luaL_error(L, "import: module name must not be empty");
	}
	auto* importer = GetImporter(L);
	if (!importer) {
		return luaL_error(L, "import: system not initialized");
	}
	return importer->Import(L, std::string_view(name, len));
}

int l_import_setpath(lua_State* L) {
	size_t len = 0;
	const char* paths = luaL_checklstring(L, 1, &len);
	auto* importer = GetImporter(L);
	if (!importer) {
		return luaL_error(L, "import: system not initialized");
	}
	importer->SetPaths(std::string(paths, len));
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: setpath [{}]", std::string(paths, len));
	return 0;
}

int l_import_addpath(lua_State* L) {
	size_t len = 0;
	const char* path = luaL_checklstring(L, 1, &len);
	auto* importer = GetImporter(L);
	if (!importer) {
		return luaL_error(L, "import: system not initialized");
	}
	importer->AddPath(std::string(path, len));
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: addpath [{}]", std::string(path, len));
	return 0;
}

int l_import_loaded(lua_State* L) {
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	lua_remove(L, -2);
	// Return a shallow copy so scripts can inspect what's loaded without
	// accidentally mutating the real package.loaded table.
	lua_newtable(L);  // orig, copy
	lua_pushnil(L);	 // orig, copy, nil
	while (lua_next(L, -3)) {  // orig, copy, k, v
		lua_pushvalue(L, -2);  // orig, copy, k, v, k
		lua_pushvalue(L, -2);  // orig, copy, k, v, k, v
		lua_rawset(L, -5);	// orig, copy, k
	}
	lua_remove(L, -2);	// copy
	return 1;
}

int l_import_clearcache(lua_State* L) {
	auto* importer = GetImporter(L);
	if (!importer) {
		return luaL_error(L, "import: system not initialized");
	}
	importer->ClearCache(L);
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: cache cleared");
	return 0;
}

// Wrapper: when the import table is called as import("mod"), Lua invokes
// the __call metamethod with (table, "mod").  Remove the table so l_import
// sees the module name at index 1.
int l_import_call(lua_State* L) {
	lua_remove(L, 1);
	return l_import(L);
}

}  // namespace

// Public API

void ExportImport(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return;

	// Create the per-VM importer and store its pointer in the Lua registry
	auto& importer = vm.GetImporter();
	lua_pushlightuserdata(L, &importer);
	lua_setfield(L, LUA_REGISTRYINDEX, "__ScriptImporter");

	// Build the "import" callable table.  Lua 5.4 does not allow setting
	// fields on a C function, so we use a table with a __call metamethod.
	//   import("mod")      → __call dispatches to l_import
	//   import.setpath(…)  → table field access
	lua_newtable(L);  // t

	lua_pushcfunction(L, l_import_setpath);	 // t, f
	lua_setfield(L, -2, "setpath");	 // t
	lua_pushcfunction(L, l_import_addpath);	 // t, f
	lua_setfield(L, -2, "addpath");	 // t
	lua_pushcfunction(L, l_import_loaded);	// t, f
	lua_setfield(L, -2, "loaded");	// t
	lua_pushcfunction(L, l_import_clearcache);	// t, f
	lua_setfield(L, -2, "clearcache");	// t

	// Make the table callable via __call metamethod.
	lua_newtable(L);  // t, mt
	lua_pushcfunction(L, l_import_call);  // t, mt, f
	lua_setfield(L, -2, "__call");	// t, mt
	lua_setmetatable(L, -2);  // t

	lua_setglobal(L, "import");	 // (empty)

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ScriptBind: import module exported "
					"(import / import.setpath / import.addpath / "
					"import.loaded / import.clearcache)");
}

}  // namespace engine
