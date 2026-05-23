#include "engine/script/import_bind.h"

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/vm/script_importer.h"
#include "engine/vm/vm.h"

extern "C" {
#include "3rdparty/lua/lua.h"
#include "3rdparty/lua/lauxlib.h"
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

} // namespace

//=============================================================================
// Public API
//=============================================================================

void ExportImport(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    // Create the per-VM importer and store its pointer in the Lua registry
    auto& importer = vm.GetImporter();
    lua_pushlightuserdata(L, &importer);
    lua_setfield(L, LUA_REGISTRYINDEX, "__ScriptImporter");

    // Build the "import" function with sub-functions as fields.
    // In Lua you can call: import("mod") and import.setpath("...")
    lua_pushcfunction(L, l_import);               // import function
    lua_pushcfunction(L, l_import_setpath);       // import.setpath
    lua_setfield(L, -2, "setpath");
    lua_pushcfunction(L, l_import_addpath);       // import.addpath
    lua_setfield(L, -2, "addpath");
    lua_pushcfunction(L, l_import_loaded);        // import.loaded
    lua_setfield(L, -2, "loaded");
    lua_pushcfunction(L, l_import_clearcache);    // import.clearcache
    lua_setfield(L, -2, "clearcache");
    lua_setglobal(L, "import");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: import module exported "
                    "(import / import.setpath / import.addpath / "
                    "import.loaded / import.clearcache)");
}

} // namespace engine
