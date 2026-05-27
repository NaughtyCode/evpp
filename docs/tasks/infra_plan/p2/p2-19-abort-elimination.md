# P2-19: Eliminate 4 abort() Calls — Graceful Error Handling

## Objective

Replace all 4 `abort()` calls with graceful error handling: log the error and return/throw/exit cleanly rather than crashing the entire process.

## Current State

Four `abort()` calls exist:

| Location | Trigger | Severity |
|----------|---------|----------|
| `vm.cc:20` | `luaL_newstate()` returns nullptr | P2 |
| `engine.cc:53` | `GetScriptVM()` called before `Init()` | P2 |
| `event_loop.cc:38` | `event_base_new()` fails | P2 |
| `event_loop.cc:196` | `event_reinit()` fails in `AfterFork()` | P2 |

Each `abort()` terminates the entire process, killing all online players and losing all unsaved data. For a server, graceful degradation is always better than hard crash.

## Implementation Steps

### Step 1: vm.cc — luaL_newstate Failure

```cpp
/* Before: */
L_ = luaL_newstate();
if (!L_) {
    fprintf(stderr, "Fatal: Failed to create Lua state\n");
    abort();
}

/* After: */
L_ = luaL_newstate();
if (!L_) {
    ENGINE_LOG_CRITICAL("Failed to create Lua state — out of memory or system limit");
    throw std::runtime_error("Failed to create Lua state");
    /* Caller (Engine::Init) catches and returns InitResult::Failure */
}
```

### Step 2: engine.cc — GetScriptVM Before Init

```cpp
/* Before: */
ScriptVM& Engine::GetScriptVM() {
    if (!script_vm_) {
        fprintf(stderr, "Fatal: GetScriptVM() called before Init()\n");
        abort();
    }
    return *script_vm_;
}

/* After: */
ScriptVM& Engine::GetScriptVM() {
    if (!script_vm_) {
        ENGINE_LOG_CRITICAL("GetScriptVM() called before Init() — check initialization order");
        throw std::logic_error("ScriptVM not initialized — call Engine::Init() first");
    }
    return *script_vm_;
}
```

### Step 3: event_loop.cc — event_base_new Failure

```cpp
/* Before: */
base_ = event_base_new();
if (!base_) {
    fprintf(stderr, "Fatal: event_base_new() failed\n");
    abort();
}

/* After: */
base_ = event_base_new();
if (!base_) {
    ENGINE_LOG_CRITICAL("event_base_new() failed — system may be out of resources");
    return false;  /* propagate to caller's Init() method */
}
```

### Step 4: event_loop.cc — event_base_dispatch Twice

```cpp
/* Before: */
if (dispatching_) {
    fprintf(stderr, "Fatal: event_base_dispatch() called while already dispatching\n");
    abort();
}

/* After: */
if (dispatching_) {
    ENGINE_LOG_ERROR("event_base_dispatch() called while already dispatching — ignored");
    return;  /* no-op instead of crash */
}
```

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/abort_elimination_test.cc`:
- Former abort(1) site: error path triggers log + throw (or return false), no process termination
- Former assert(false) site: error path triggers log + graceful return
- Engine shutdown after each error: verify clean exit without crash
- GoogleTest death tests: verify specific error paths no longer call abort()

**Integration tests** — `src/tests/integration/shutdown_safety_test.cc` (shared, add cases):
- Init failure → shutdown → no crash, clean exit
- Start failure → shutdown → no crash, clean exit
- All 4 former abort sites produce descriptive error messages

```
src/tests/unit/abort_elimination_test.cc         # ~60 lines
src/tests/integration/shutdown_safety_test.cc     # +30 lines (extend existing)
```

## Acceptance Criteria

1. Zero `abort()` / `std::abort()` calls in `src/runtime/`
2. All 4 former abort sites use: log + throw / log + return false / log + no-op
3. Graceful shutdown is possible after each error
4. Error messages are descriptive (actionable by operator)
5. Tests verify graceful handling of each error path

## Dependencies: None | Estimated Effort: ~80 lines
