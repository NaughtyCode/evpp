# P2-23: DoString Script Source Restriction + Execution Limits

## Objective

Add script source tracking, maximum execution time, and memory quota to `ScriptVM::DoString` to prevent runaway or malicious Lua code execution.

## Current State

`ScriptVM::DoString` (`vm.cc:115-158`) can execute Lua code from any source with no restrictions:
- No caller identity tracking
- No maximum execution time (infinite loop = server hang)
- No memory quota (infinite allocations = OOM)

An attacker who can inject code into a `DoString` call (e.g., via a compromised config file or network input) can execute unbounded Lua code.

## Implementation Steps

### Step 1: Add Execution Context

**File**: `src/runtime/vm/vm.h`

```cpp
struct ExecuteContext {
    std::string source;          /* "config", "network", "file:path" */
    std::string caller_id;       /* identifying who triggered execution */
    int max_execution_ms = 5000; /* max time before timeout, 0 = unlimited */
    int64_t max_memory_bytes = 10 * 1024 * 1024; /* 10MB quota */
};
```

### Step 2: Instrument DoString with Timeout Hook

**File**: `src/runtime/vm/vm.cc`

```cpp
bool ScriptVM::DoString(const std::string& code, const ExecuteContext& ctx) {
    /* Install instruction-count hook for timeout enforcement */
    int instruction_count = 0;
    const int kMaxInstructions = ctx.max_execution_ms * 100; /* rough estimate */

    lua_sethook(L_, [](lua_State* L, lua_Debug* ar) {
        /* Increment counter; if exceeded, error out */
        auto* self = static_cast<ScriptVM*>(lua_touserdata(L, ...));
        self->instruction_count_++;
        if (self->instruction_count_ > self->max_instructions_) {
            luaL_error(L, "Script execution timed out (max %d ms)",
                       self->max_execution_ms_);
        }
    }, LUA_MASKCOUNT, 1000);  /* check every 1000 instructions */

    /* Set memory limit via Lua GC */
    lua_gc(L_, LUA_GCCOLLECT, 0);  /* force GC before limit */
    /* Track memory: lua_gc(L_, LUA_GCCOUNT, 0) returns KB used */
    initial_memory_ = lua_gc(L_, LUA_GCCOUNT, 0);

    int result = luaL_dostring(L_, code.c_str());

    /* Remove hook */
    lua_sethook(L_, nullptr, 0, 0);

    if (result != LUA_OK) {
        ENGINE_LOG_ERROR("DoString failed [source={}, caller={}]: {}",
                         ctx.source, ctx.caller_id, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    /* Check memory usage */
    int64_t final_memory = lua_gc(L_, LUA_GCCOUNT, 0);
    if ((final_memory - initial_memory_) * 1024 > ctx.max_memory_bytes) {
        ENGINE_LOG_WARN("DoString exceeded memory quota [source={}, caller={}]: "
                        "{} KB used (limit {} KB)",
                        ctx.source, ctx.caller_id,
                        final_memory - initial_memory_, ctx.max_memory_bytes / 1024);
        /* Trigger GC to reclaim */
        lua_gc(L_, LUA_GCCOLLECT, 0);
    }

    return true;
}
```

### Step 3: Memory Quota Enforcement

Use Lua's `LUA_MASKCOUNT` hook to also check memory usage periodically. If memory exceeds the quota, trigger an error to halt execution.

### Step 4: Source-Based Policy

**File**: `src/runtime/config/runtime_config.h`

```cpp
struct ScriptSourcePolicy {
    std::string source_pattern;     /* "network:*", "file:scripts/*", "config" */
    int max_execution_ms = 5000;
    int64_t max_memory_bytes = 10 * 1024 * 1024;
    bool allowed = true;            /* false = reject this source entirely */
};
```

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/dostring_limits_test.cc`:
- DoString from "network" source with 2s timeout → script within limit succeeds
- DoString with infinite loop → terminated by instruction-count timeout hook
- DoString with large allocations (exceeding memory quota) → quota check triggers, execution stopped
- Disallowed source ("network") → DoString returns false, script not executed
- Normal script execution → no measurable performance regression from hook overhead
- Timeout and quota violations produce log messages with source info

```
src/tests/unit/dostring_limits_test.cc   # ~60 lines
```

## Acceptance Criteria

1. `DoString` accepts an `ExecuteContext` with source, caller, limits
2. Execution timeout enforced via Lua instruction-count hook
3. Memory quota checked after execution
4. Source-based execution policy is configurable
5. Timeout and quota violations produce clear log messages
6. Tests verify timeout, memory quota, and source blocking

## Dependencies

- P0-7 (Lua Sandbox) — sandbox limits which libraries are available

## Estimated Effort: ~100 lines
