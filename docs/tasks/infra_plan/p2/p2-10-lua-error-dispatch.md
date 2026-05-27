# P2-10: Lua Error Dispatch Strategy Unification

## Objective

Unify the four different Lua error dispatch patterns used across subsystems into a single, consistent error handling strategy.

## Current State

Four different error dispatch patterns coexist:

| Subsystem | Pattern | Behavior on error |
|-----------|---------|-------------------|
| HTTP | call_lua_http_handler wrapper | Error logged via lua_pcall, stack popped |
| TCP | CallInstMethodStr wrapper | Error logged via lua_pcall, stack popped |
| KCP | Direct lua_pcall | Error logged, stack popped |
| Timer | Direct lua_pcall (timer_bind.cc:67) | Error logged, stack popped |

No unified policy exists for:
- Error logging format (none include Lua traceback — all just log the error string)
- Error propagation (all silently swallow — no caller notification)
- Stack cleanup (inconsistent lua_pop behavior after errors)

## Implementation Steps

### Step 1: Define Unified Error Handler

**File**: `src/runtime/vm/lua_error_handler.h`

```cpp
/*
 * Unified Lua error handling.
 *
 * All Lua C API calls that invoke Lua functions should use
 * SafeCallLua() or SafeCallLuaMethod() for consistent error behavior.
 *
 * Error actions:
 *   1. Log the error with Lua callstack traceback
 *   2. Pop any partial results from the Lua stack
 *   3. Optionally notify a global error handler (for monitoring)
 *   4. Return a status code to the caller
 */
enum class LuaCallResult {
    Ok,
    LuaError,    /* Lua runtime error */
    NotFound,    /* Function not found */
    Disposed,    /* Target object has been disposed */
};

struct LuaCallOptions {
    int nargs = 0;
    int nresults = 0;
    bool log_on_error = true;        /* always true in production */
    bool throttle_repeated = true;   /* throttle repeated errors (1/sec) */
    int error_handler_ref = 0;       /* custom error handler (0 = default) */
};

LuaCallResult SafeCallLua(lua_State* L, LuaCallOptions opts);
LuaCallResult SafeCallLuaMethod(lua_State* L, int instance_ref,
                                 const char* method_name, LuaCallOptions opts);
```

### Step 2: Add Traceback to All Error Logs

```cpp
/* Default error message handler — pushes traceback function */
static int LuaErrorHandler(lua_State* L) {
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    return 1;
}

LuaCallResult SafeCallLua(lua_State* L, LuaCallOptions opts) {
    /* Push error handler */
    lua_pushcfunction(L, LuaErrorHandler);
    int err_handler_idx = lua_gettop(L) - opts.nargs - 1;

    /* Move function before arguments */
    lua_insert(L, err_handler_idx);

    int result = lua_pcall(L, opts.nargs, opts.nresults, err_handler_idx);

    if (result != LUA_OK) {
        if (opts.log_on_error) {
            LogLuaError(L, result);
        }
        /* Remove error handler from stack */
        lua_remove(L, err_handler_idx);
        return LuaCallResult::LuaError;
    }

    /* Remove error handler */
    lua_remove(L, err_handler_idx);
    return LuaCallResult::Ok;
}
```

### Step 3: Migrate All Call Sites

Replace all four patterns with `SafeCallLua` / `SafeCallLuaMethod`:
- HTTP binding: replace call_lua_http_handler
- TCP binding: replace CallInstMethodStr
- KCP binding: replace direct lua_pcall
- Timer binding: replace direct lua_pcall

### Step 4: Add Error Rate Limiting

```cpp
/* Throttle repeated errors: log at most 1/second per function */
static thread_local std::unordered_map<std::string, int64_t> last_error_time;

static bool ShouldLogError(const std::string& key) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto it = last_error_time.find(key);
    if (it != last_error_time.end() && (now - it->second) < 1'000'000'000) {
        return false;  /* too soon */
    }
    last_error_time[key] = now;
    return true;
}
```

### Step 5: Tests

- `SafeCallLua` on valid function → returns Ok, results on stack
- `SafeCallLua` on runtime error → returns LuaError, stack clean
- `SafeCallLua` on disposed object → returns Disposed
- Error throttling: rapid errors → only first is logged within 1s window
- Traceback is included in error log

## Acceptance Criteria

1. All 4 error dispatch patterns replaced with `SafeCallLua`/`SafeCallLuaMethod`
2. All errors include Lua traceback in log message
3. Error rate limiting prevents log flooding from rapid errors
4. Lua stack is clean after error (no stray values)
5. Error metrics: `lua_errors_total` counter increments on each error

## Dependencies: None | Estimated Effort: ~200 lines
