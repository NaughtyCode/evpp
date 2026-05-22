#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine_export.h"

extern "C" {
#include "3rdparty/lua/lua.h"
#include "3rdparty/lua/lauxlib.h"
#include "3rdparty/lua/lualib.h"
}

namespace engine {

//=============================================================================
// ScriptVM — RAII wrapper around a Lua lua_State.
//
// Exposes the raw lua_State* via GetState() so callers have full access to
// the Lua C API for stack operations, table manipulation, coroutine control,
// debug hooks, etc.
//=============================================================================

class ENGINE_API ScriptVM {
public:
    //=================================================================
    // Construction / destruction
    //=================================================================

    ScriptVM();
    ~ScriptVM();

    ScriptVM(const ScriptVM&) = delete;
    ScriptVM& operator=(const ScriptVM&) = delete;
    ScriptVM(ScriptVM&& other) noexcept;
    ScriptVM& operator=(ScriptVM&& other) noexcept;

    //=================================================================
    // Raw state access — use this for any Lua C API call not directly
    // wrapped by this class.
    //=================================================================

    lua_State* GetState() { return L_; }
    const lua_State* GetState() const { return L_; }

    //=================================================================
    // Script lifecycle — calls the corresponding global Lua function
    // if it exists. Silently no-ops when the function is not defined.
    //=================================================================

    void InitScript();
    void UpdateScript();
    void DestroyScript();

    //=================================================================
    // Script execution
    //=================================================================

    // Execute a Lua string. Returns true on success.
    // On error the message is logged and returned via `error_out`.
    bool DoString(std::string_view script,
                  std::string_view chunk_name = "string",
                  std::string* error_out = nullptr);

    // Execute a Lua file. Returns true on success.
    bool DoFile(const std::string& filename,
                std::string* error_out = nullptr);

    // Execute all .lua files found directly in a directory (non-recursive).
    // Returns the number of files that failed.
    size_t DoDirectory(const std::string& dir_path);

    //=================================================================
    // C function / module registration
    //=================================================================

    // Register a single C function as a global.
    void RegisterFunction(std::string_view name, lua_CFunction func);

    // Register every function in a luaL_Reg array (terminated by {NULL,NULL})
    // as individual globals.
    void RegisterFunctions(const luaL_Reg* functions);

    // Register a luaL_Reg array as a named module (a global table).
    // Uses luaL_newlib / luaL_setfuncs internally.
    void RegisterModule(std::string_view name, const luaL_Reg* functions);

    // Register a module with an open function (luaL_requiref style).
    // The open function receives the lua_State and should push the module
    // table; it will be registered in package.preload[name] for on-demand
    // loading via require().
    void RegisterModuleOpen(std::string_view name, lua_CFunction openf,
                            bool make_global = true);

    //=================================================================
    // Convenience getters / setters for globals
    //=================================================================

    template <typename T>
    void SetGlobal(std::string_view name, T value);

    //=================================================================
    // Convenience: register a lambda / std::function as a global
    //=================================================================

    // The callback receives (lua_State*) and returns number of return values
    // pushed on the Lua stack, following Lua C calling convention.
    using LuaCallback = std::function<int(lua_State*)>;

    void RegisterCallback(std::string_view name, LuaCallback callback);

    //=================================================================
    // Utilities
    //=================================================================

    // Pop the value at the top of the stack and return it as a string.
    std::string ToString(int index = -1);

    // Return the Lua version string.
    static const char* LuaVersion();

private:
    // Shared trampoline storage for RegisterCallback.
    static int CallbackTrampoline(lua_State* L);

    // Call a global Lua function by name (0 args, 0 results).
    // Logs a warning if the function exists but errors at runtime.
    void CallGlobalFunction(std::string_view name);

    lua_State* L_ = nullptr;

    // Keep callback objects alive at stable addresses (lightuserdata
    // pointers captured by Lua closures must not dangle across reallocations).
    std::vector<std::unique_ptr<LuaCallback>> callbacks_;
};

//=============================================================================
// Template implementations
//=============================================================================

template <>
inline void ScriptVM::SetGlobal(std::string_view name, int value) {
    lua_pushinteger(L_, static_cast<lua_Integer>(value));
    lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, double value) {
    lua_pushnumber(L_, static_cast<lua_Number>(value));
    lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, const char* value) {
    lua_pushstring(L_, value);
    lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, std::string_view value) {
    lua_pushlstring(L_, value.data(), value.size());
    lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, bool value) {
    lua_pushboolean(L_, value ? 1 : 0);
    lua_setglobal(L_, std::string(name).c_str());
}

} // namespace engine
