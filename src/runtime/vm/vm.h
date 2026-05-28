#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/vm/custom_ptr_store.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/script_importer.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace engine {

// ScriptVM — RAII wrapper around a Lua lua_State.
//
// Exposes the raw lua_State* via GetState() so callers have full access to
// the Lua C API for stack operations, table manipulation, coroutine control,
// debug hooks, etc.

class ENGINE_API ScriptVM {
	public:
	// Construction / destruction

	ScriptVM(LuaSandboxLevel level = LuaSandboxLevel::Full);
	virtual ~ScriptVM();

	ScriptVM(const ScriptVM&) = delete;
	ScriptVM& operator=(const ScriptVM&) = delete;
	ScriptVM(ScriptVM&& other) noexcept;
	ScriptVM& operator=(ScriptVM&& other) noexcept;

	// Raw state access — use this for any Lua C API call not directly
	// wrapped by this class.

	lua_State* GetState() {
		return L_;
	}
	const lua_State* GetState() const {
		return L_;
	}

	// Script lifecycle — calls the corresponding global Lua function
	// if it exists. Silently no-ops when the function is not defined.

	void InitScript();
	void UpdateScript();
	void DestroyScript();

	// Script execution

	// Execute a Lua string. Returns true on success.
	// On error the message is logged and returned via `error_out`.
	// If result_out is non-null and the script returns a string,
	// it is captured via lua_tostring(L, -1) after successful execution.
	bool DoString(std::string_view script,
				  std::string_view chunk_name = "string",
				  std::string* error_out = nullptr,
				  std::string* result_out = nullptr);

	// Execute a Lua file. Returns true on success.
	bool DoFile(const std::string& filename, std::string* error_out = nullptr);

	// Execute all .lua files found directly in a directory (non-recursive).
	// Returns the number of files that failed.
	size_t DoDirectory(const std::string& dir_path);

	// C function / module registration

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
	void RegisterModuleOpen(std::string_view name, lua_CFunction openf, bool make_global = true);

	// Custom pointer store — per-VM void* array (backed by global_State)

	// Pre-allocate capacity for at least 'total_slots' pointers.
	// Returns true on success, false on allocation failure.
	bool ReserveCustomPtrSlots(int total_slots);

	// Store a pointer at a 1-based index.  If index > Count()+1 the
	// array is extended and intermediate slots are filled with nullptr.
	// Index 0 or out-of-range negative indices are no-ops.
	void SetCustomPtr(int index, void* ptr);

	// Return the pointer at a 1-based index, or nullptr if out of range
	// or if the slot genuinely stores nullptr.
	void* GetCustomPtr(int index) const;

	// Typed variant of GetCustomPtr.
	template <typename T>
	T* GetCustomPtrAs(int index) const {
		return static_cast<T*>(GetCustomPtr(index));
	}

	// Append a pointer; returns its new 1-based index.  Returns 0 on
	// allocation failure.
	int PushCustomPtr(void* ptr);

	// Set the slot at a 1-based index to nullptr.  The slot stays in the
	// array; Count() is unchanged and higher indices are undisturbed.
	// Out-of-range indices (0, or beyond Count()) are silent no-ops.
	void SetNullCustomPtr(int index);

	// Drop all stored pointers (length = 0).  Capacity is preserved.
	void ClearCustomPtrs();

	// Number of pointers currently stored.
	int CustomPtrCount() const;

	// Current allocated capacity.
	int CustomPtrCapacity() const;

	// True if the array has a (non-null) pointer at the given index.
	bool HasCustomPtr(int index) const;

	// Find the 1-based index of a pointer value, or -1 if not found.
	int FindCustomPtr(void* ptr) const;

	// True if the pointer value exists in the array.
	bool ContainsCustomPtr(void* ptr) const;

	// Copy at most max_count pointers into dst.  Returns the number
	// written (min(Count(), max_count)).
	int CopyCustomPtrsTo(void** dst, int max_count) const;

	// Replace the entire array with count pointers from src.
	void CopyCustomPtrsFrom(void* const* src, int count);

	// Convenience getters / setters for globals

	template <typename T>
	void SetGlobal(std::string_view name, T value);

	// Convenience: register a lambda / std::function as a global

	// The callback receives (lua_State*) and returns number of return values
	// pushed on the Lua stack, following Lua C calling convention.
	using LuaCallback = std::function<int(lua_State*)>;

	void RegisterCallback(std::string_view name, LuaCallback callback);

	// Utilities

	// Pop the value at the top of the stack and return it as a string.
	std::string ToString(int index = -1);

	// Return the Lua version string.
	static const char* LuaVersion();

	// Module import system

	ScriptImporter& GetImporter();
	void SetImportPath(const std::string& scripts_dir);

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

	std::unique_ptr<ScriptImporter> importer_;
};

// Template implementations

template <>
inline void ScriptVM::SetGlobal(std::string_view name, int value) {
	if (!L_) return;
	lua_pushinteger(L_, static_cast<lua_Integer>(value));
	lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, double value) {
	if (!L_) return;
	lua_pushnumber(L_, static_cast<lua_Number>(value));
	lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, const char* value) {
	if (!L_) return;
	lua_pushstring(L_, value);
	lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, std::string_view value) {
	if (!L_) return;
	lua_pushlstring(L_, value.data(), value.size());
	lua_setglobal(L_, std::string(name).c_str());
}

template <>
inline void ScriptVM::SetGlobal(std::string_view name, bool value) {
	if (!L_) return;
	lua_pushboolean(L_, value ? 1 : 0);
	lua_setglobal(L_, std::string(name).c_str());
}

}  // namespace engine
