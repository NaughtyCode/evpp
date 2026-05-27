# P1-7: Lua Coroutine Integration

## Objective

Provide engine-level support for Lua coroutines, enabling async/await-style patterns for network requests, database queries, and timer-based delays without callback nesting.

## Current State

Lua 5.5 supports coroutines natively (`coroutine.create`, `coroutine.resume`, `coroutine.yield`), but the engine provides **zero scheduling or integration**. All Lua callbacks are synchronous — network messages, timer callbacks, and database responses are delivered via callbacks, leading to callback nesting in complex flows:

```lua
-- Current: nested callbacks
conn:send_request("login", {user = "alice"}, function(reply)
    if reply.ok then
        db_send_request("find", {uid = reply.uid}, function(db_result)
            conn:send("welcome_back")
        end)
    end
end)
```

## Root Cause

The engine's update loop runs synchronously: `InitScript → UpdateScript → DestroyScript`. No mechanism exists to suspend a Lua coroutine and resume it when an async operation completes.

## Implementation Steps

### Step 1: Define Coroutine Scheduler

**File**: `src/runtime/vm/coroutine_scheduler.h`

```cpp
/*
 * CoroutineScheduler manages cooperative multitasking for Lua coroutines.
 *
 * Each coroutine has a state: Running, Suspended (waiting for async result),
 * or Dead (completed or errored).
 *
 * The scheduler is driven by Engine::Update() — each frame, it resumes
 * all runnable coroutines.
 */
class CoroutineScheduler {
public:
    /*
     * Create a new coroutine from a Lua function.
     * Returns a handle for later cancellation.
     */
    int CreateCoroutine(lua_State* L, int function_ref);

    /*
     * Resume all runnable coroutines.
     * Called each frame from Engine::Update().
     */
    void Update();

    /*
     * Resume a specific coroutine (called when its async operation completes).
     */
    void ResumeCoroutine(int handle, int num_results);

    /*
     * Cancel and clean up a coroutine.
     */
    void CancelCoroutine(int handle);

    /*
     * Number of active coroutines.
     */
    size_t ActiveCount() const;

private:
    struct CoroutineState {
        lua_State* thread = nullptr;   /* lua_newthread result */
        int handle = 0;
        int function_ref = LUA_NOREF;
        enum { Suspended, Runnable, Dead } state = Suspended;
    };

    std::unordered_map<int, CoroutineState> coroutines_;
    int next_handle_ = 1;
};
```

### Step 2: Implement Async Primitives

**File**: `src/runtime/vm/coroutine_scheduler.cc`

```cpp
/*
 * The core pattern:
 *
 * In Lua:
 *   local result = async_call(function()
 *       return db_query("find", {uid = 123})
 *   end)
 *
 * The async_call function:
 *   1. Creates a coroutine from the provided function
 *   2. Yields the calling coroutine
 *   3. The async operation (C++ side) is initiated
 *   4. When complete, the callback resumes the coroutine with the result
 */

/* C++ helper: schedule coroutine resume after async op */
template<typename AsyncOp>
int StartAsyncOperation(lua_State* L, AsyncOp&& op) {
    /* Get current coroutine handle from Lua registry */
    int handle = GetCurrentCoroutineHandle(L);

    /* Initiate the async operation — pass the resume callback */
    std::forward<AsyncOp>(op)([L, handle](auto... results) {
        /* Called when async op completes — push results and resume */
        CoroutineScheduler::Instance().ResumeCoroutine(handle, sizeof...(results));
    });

    /* Suspend the current coroutine */
    return lua_yield(L, 0);
}
```

### Step 3: Lua Async API

**File**: `resources/script/async.lua`

```lua
--[[
    Async helper module for coroutine-based async programming.

    Usage:
        local result = async.await(function()
            return async.db_query("find", {uid = 123})
        end)

        local reply = async.await(function()
            return async.net_request(conn, "login", {user = "alice"})
        end)

        async.sleep(1000)  -- wait 1 second (coroutine suspends)
]]

local async = {}

function async.await(fn)
    local co = coroutine.create(fn)
    local ok, result = coroutine.resume(co)
    if not ok then
        error(result)
    end
    return result
end

function async.sleep(ms)
    -- Suspend coroutine, timer callback resumes it
    local co = coroutine.running()
    timer.timeout(ms, function()
        coroutine.resume(co)
    end)
    return coroutine.yield()
end

function async.db_query(op, query)
    local co = coroutine.running()
    db_send_request(op, query, function(ok, err, result)
        -- Resume coroutine with results
        coroutine.resume(co, ok, err, result)
    end)
    return coroutine.yield()
end

function async.net_request(conn, msg_type, data)
    local co = coroutine.running()
    conn:send_request(msg_type, data, function(reply)
        coroutine.resume(co, reply)
    end)
    return coroutine.yield()
end

return async
```

### Step 4: Engine Integration

**File**: `src/runtime/engine/engine.cc`

```cpp
/* In Engine::Start() — initialize coroutine scheduler */
CoroutineScheduler::Instance().Init(script_vm_->GetState());

/* In Engine::Update() — resume runnable coroutines each frame */
CoroutineScheduler::Instance().Update();
```

### Step 5: Add Coroutine Limits

**File**: `src/runtime/config/runtime_config.h`

```cpp
struct RuntimeConfig {
    uint32_t max_coroutines = 10000;         /* Maximum concurrent coroutines */
    uint32_t max_coroutine_yield_ms = 10;    /* Max time per frame for coroutine resumption */
    /* ... */
};
```

### Step 6: Tests

**File**: `src/tests/unit/coroutine_scheduler_test.cc`

- Create coroutine, yield, resume — verify result delivered
- Multiple coroutines — verify all resume correctly
- Coroutine cancellation — verify cleanup
- Error in coroutine — verify error propagation
- Max coroutines limit — verify enforcement

**File**: `src/tests/lua/async_test.lua`

- `async.sleep(100)` suspends and resumes after 100ms
- `async.await` with database query returns correct result
- Error propagation from async operations
- Multiple concurrent async operations

## Acceptance Criteria

1. `CoroutineScheduler` manages coroutine lifecycle (create/suspend/resume/cancel)
2. Coroutines are resumed each frame via `Engine::Update()`
3. `async.lua` module provides `sleep`, `await`, `db_query`, `net_request`
4. Coroutine limits are configurable and enforced
5. Error in coroutine does not crash the engine
6. Tests verify create/suspend/resume/cancel, multiple coroutines, error handling

## Dependencies

- None (uses existing Lua coroutine support; does not require P0-1 or P0-2)

## Estimated Effort

- `CoroutineScheduler` class: ~150 lines
- Engine integration: ~20 lines
- `async.lua` module: ~80 lines
- Lua binding: ~50 lines
- Config: ~10 lines
- Tests: ~150 lines C++ + ~100 lines Lua
- **Total**: ~500 lines

## Risks

- **C stack overflow**: Deeply nested coroutine resumes could overflow the C stack. Lua 5.5 handles this better with its new stack management, but limits are still needed.
- **Memory**: Each suspended coroutine holds a Lua thread stack. With 10,000 coroutines at 1KB each, that's 10MB — manageable but worth monitoring.
- **Debug complexity**: Coroutine callstacks are harder to debug than synchronous callbacks. Mitigation: add coroutine ID to all log messages from within coroutine context.
- **Deadlock**: If two coroutines await each other, they deadlock. Lua's cooperative scheduling means no true deadlock, but logical starvation is possible. Add coroutine timeouts.
