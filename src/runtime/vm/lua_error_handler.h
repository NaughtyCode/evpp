#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

// Unified Lua error dispatch result.
enum class LuaCallResult {
	Ok,
	LuaError,    // Lua runtime error
	NotFound,    // Function not found
	Disposed,    // Target object has been disposed
};

// Options for SafeCallLua / SafeCallLuaMethod.
struct LuaCallOptions {
	int nargs = 0;
	int nresults = 0;
	bool log_on_error = true;
	bool throttle_repeated = true;
	int error_handler_ref = 0;  // custom error handler (0 = default with traceback)
};

// Push the default Lua traceback error handler and return its stack index.
// Caller must remove it from the stack after lua_pcall.
inline int PushLuaErrorHandler(lua_State* L) {
	lua_pushcfunction(L, [](lua_State* L2) -> int {
		luaL_traceback(L2, L2, lua_tostring(L2, -1), 1);
		return 1;
	});
	return lua_gettop(L);
}

// Push an error handler and position it below the function about to be called.
// Returns the stack index of the error handler (usable as msgh for lua_pcall).
//
// Before:  ... [function] [arg1] ... [argN]
// After:   ... [err_handler] [function] [arg1] ... [argN]
//
// The caller then calls lua_pcall(L, nargs, nresults, msgh) with the
// returned value as msgh. lua_pcall leaves the message handler on the stack;
// callers must remove it on success or restore the previous stack on failure.
inline int PushLuaErrorHandlerForCall(lua_State* L, int nargs) {
	int f_idx = lua_gettop(L) - nargs;
	int err_idx = PushLuaErrorHandler(L);
	(void) err_idx;
	lua_insert(L, f_idx);
	return f_idx;  // error handler is now at f_idx
}

// Throttle repeated error logging. Returns true if the error should be logged.
inline bool ShouldLogError(const std::string& key) {
	static thread_local std::unordered_map<std::string, int64_t> last_error_time;
	auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
				   std::chrono::steady_clock::now().time_since_epoch()).count();
	auto it = last_error_time.find(key);
	if (it != last_error_time.end() && (now - it->second) < 1'000'000'000) {
		return false;
	}
	last_error_time[key] = now;
	return true;
}

// Call a Lua function safely with error handling and traceback.
//
// Stack layout before the call:
//   ... [function] [args...]
// After PushLuaErrorHandler + lua_insert:
//   ... [err_handler] [function] [args...]
//
// lua_pcall with msgh=func_idx removes the error handler, function,
// and args, then places nresults (or the error message) at func_idx.
//
// On success, nresults are on the stack starting at func_idx.
// On error, the error message is at func_idx.  The caller may pop
// or leave it as needed.
inline LuaCallResult SafeCallLua(lua_State* L, LuaCallOptions opts) {
	int func_idx = lua_gettop(L) - opts.nargs;

	// func_idx <= 0 means the stack is too shallow for the claimed
	// nargs — the "function" position doesn't exist.
	if (func_idx <= 0 || !lua_isfunction(L, func_idx)) {
		if (func_idx > 0) {
			lua_settop(L, func_idx - 1);
		} else {
			// Pop whatever args were pushed on top, but never more than
			// what's actually on the stack.
			lua_pop(L, std::min(opts.nargs, lua_gettop(L)));
		}
		return LuaCallResult::NotFound;
	}

	if (opts.error_handler_ref != 0 && opts.error_handler_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, opts.error_handler_ref);
		if (!lua_isfunction(L, -1)) {
			lua_pop(L, 1);
			PushLuaErrorHandler(L);
		}
	} else {
		PushLuaErrorHandler(L);
	}
	lua_insert(L, func_idx);  // move error handler below function

	int rc = lua_pcall(L, opts.nargs, opts.nresults, func_idx);

	if (rc != LUA_OK) {
		// On error, lua_pcall leaves the error message (and possibly
		// the message handler) on the stack.  Restore to clean state.
		lua_settop(L, func_idx - 1);
		return LuaCallResult::LuaError;
	}

	// On success, lua_pcall removes the function + args but preserves
	// the message handler at func_idx.  Results sit above it.
	lua_remove(L, func_idx);  // remove error handler; results shift down
	return LuaCallResult::Ok;
}

}  // namespace engine
