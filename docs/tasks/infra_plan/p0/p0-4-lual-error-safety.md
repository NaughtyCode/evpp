# P0-4: luaL_error Exception Safety — Prevent C++ Destructor Bypass

## Objective

Eliminate all cases where `luaL_error` (which uses `longjmp`) bypasses C++ destructors for RAII objects on the stack. Apply the fix to all 6 protocol binding files.

## Current State

`luaL_error` is implemented via `longjmp`, which unwinds the C stack **without calling C++ destructors**. Binding code uses `luaL_error` for input validation while `std::unique_ptr`, `std::string`, and other RAII objects are on the stack:

```cpp
/* net_tcp_server_bind.cc — simplified from actual code (lines 366-541) */
int l_net_server_listen(lua_State* L) {
    auto* ctx = new ServerCtx();                    /* bare new, manually managed */
    auto name = std::string("lua_server_") + ...;   /* std::string on stack */
    ctx->server = std::make_unique<evpp::TCPServer>(...);
    if (!ctx->server->Init()) {
        delete ctx;                                  /* ctx IS freed manually */
        return luaL_error(L, "server init failed");  /* longjmp! */
        /* name's ~string() never called — its heap buffer (~30 bytes) leaks.
         * ctx->server's unique_ptr destructor also skipped, but *ctx was
         * already deleted, so the TCPServer is properly freed. */
    }
}
```

Affected files (all 6 protocol bindings have `luaL_error` calls, but risk is concentrated): 
- **Active leak risk**: `net_tcp_server_bind.cc` (`l_net_server_listen` creates `std::string name` on line 393 before `luaL_error` calls at lines 524-535 for `Init()`/`Start()` failures). TCP client `l_net_client_connect` also creates `std::string name` (line 130) but has no `luaL_error` after that point — no leak risk.
- **Safe (raw pointer validation only)**: `net_kcp_server_bind.cc`, `net_kcp_client_bind.cc`, `net_udp_server_bind.cc`, `net_udp_client_bind.cc`

## Root Cause

Lua's C API uses `longjmp` for error propagation. The C++ standard guarantees local variable destruction only on normal block exit — `longjmp` is explicitly exempted (§18.10.4). When `luaL_error` longjmps, the stack is unwound in C, bypassing all C++ destructors for objects still in scope. Any RAII object with a non-trivial destructor alive on the stack leaks its resources.

## Impact

- Memory leaks: heap objects owned by `std::unique_ptr`/`std::shared_ptr` on the stack are leaked
- Resource leaks: file descriptors, locks, and other OS resources not released
- Non-deterministic: only manifests on error paths, which are rarely tested
- Debug builds: ASAN may catch these; Release builds: silent leaks

## Actual Risk Assessment (Source Code Audit)

An audit of all 6 binding files reveals that the risk is concentrated, not uniform:

**Low-risk (majority of call sites):** Most `luaL_error` calls in `l_conn_send`, `l_conn_close`, `l_conn_set_on_message`, `l_server_set_on_*`, `l_kcp_server_*`, `l_udp_server_*` are simple context validation — null pointer checks and disposed-flag checks. These have **no RAII objects on the stack** and are safe as-is.

**Real risk (concentrated in `l_*_listen` functions):** The `l_net_server_listen` function (and analogous functions in other bindings) constructs a `std::string name` for the server name on the stack, then calls `luaL_error` on `Init()`/`Start()` failure. The code already does `delete ctx` before `luaL_error`, so the heap-allocated `ServerCtx` is properly freed — but `name`'s destructor is **not** called by `longjmp`, leaking its internal heap buffer (~30 bytes per error path). The KCP and UDP server bindings avoid this by not constructing a `std::string` before their `luaL_error` calls.

**Verdict:** Fix the `l_net_server_listen` function in TCP server binding (the only binding using `std::string` for server naming before `luaL_error` calls). TCP client `l_net_client_connect` creates `std::string name` at line 130 but has no `luaL_error` calls after that point — it is safe. KCP/UDP server bindings avoid the issue by not constructing a `std::string` before their `luaL_error` calls. The other `luaL_error` call sites can be fixed opportunistically with the `LuaError` helper for consistency, but are not actively leaking.

## Implementation Steps

### Step 1: Create a Safe Error Helper

**File**: `src/runtime/script/bind_util.h`

```cpp
/*
 * LuaError — RAII-safe alternative to luaL_error.
 *
 * Pushes a formatted error string and calls lua_error.
 * Caller MUST ensure no RAII objects with non-trivial destructors
 * are alive on the stack when calling this function.
 * Use the scope-block pattern described below.
 */
int LuaError(lua_State* L, const char* fmt, ...);
```

**File**: `src/runtime/script/bind_util.cc`

```cpp
int LuaError(lua_State* L, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lua_pushstring(L, buf);
    return lua_error(L);
}
```

### Step 2: Apply the Scope-Block Fix Pattern

The core technique: **wrap all RAII objects in a nested scope block that exits before any `luaL_error` call**.

```cpp
/* BEFORE — unsafe: RAII objects alive at luaL_error call */
int l_func(lua_State* L) {
    auto guard = std::make_unique<Resource>(...);
    std::string name = "something";
    if (error) {
        return luaL_error(L, "error msg");  /* BAD: guard and name leak */
    }
    use(guard.release());
    return 0;
}

/* AFTER — safe: RAII objects destroyed before error call */
int l_func(lua_State* L) {
    Resource* raw = nullptr;
    {
        /* Scope block — all RAII objects destroyed at closing brace */
        auto guard = std::make_unique<Resource>(...);
        std::string name = "something";
        if (!error) {
            raw = guard.release();
        }
        /* If error, guard and name are destroyed here */
    }

    if (error) {
        return luaL_error(L, "error msg");  /* SAFE: no RAII on stack */
    }

    use(raw);
    return 0;
}
```

**Why this works**: The C++ standard guarantees that variables with automatic storage duration are destroyed when the block in which they are created exits (§6.7.2). By placing RAII objects in an inner scope, their destructors run at the closing brace — before any `luaL_error` call in the outer scope.

**Alternative pattern — explicit reset before error**:
```cpp
auto guard = std::make_unique<Resource>(...);
if (error) {
    guard.reset();  /* explicit destructor before longjmp */
    lua_pushstring(L, "error msg");
    return lua_error(L);
}
```

### Step 3: Fix TCP Server Binding

**File**: `src/runtime/script/net_tcp_server_bind.cc`

Apply the scope-block pattern to all `luaL_error` call sites. Audit these functions:
- `l_net_server_listen` — has `ServerCtx*`, `std::string`, `unique_ptr<TCPServer>` on stack
- `l_net_server_stop`, `l_net_server_set_message_handler`, `l_conn_send`, `l_conn_close`

### Step 4: Fix TCP Client Binding

**File**: `src/runtime/script/net_tcp_client_bind.cc`

Same scope-block pattern for `l_client_connect` and related functions.

### Step 5: Fix KCP Server Binding

**File**: `src/runtime/script/net_kcp_server_bind.cc`

Same pattern. Note: KCP binding has additional `luaL_error` calls inside `RunInLoop` callbacks — those need separate treatment (see P0-6).

### Step 6: Fix KCP Client, UDP Server, UDP Client Bindings

**Files**: `net_kcp_client_bind.cc`, `net_udp_server_bind.cc`, `net_udp_client_bind.cc`

Apply the same scope-block pattern.

### Step 7: Audit Remaining Files

```bash
grep -rn "luaL_error" src/runtime/ --include="*.cc" --include="*.h"
```

Apply fix to any files beyond the 6 protocol bindings.

### Step 8: Tests

**File**: `src/tests/unit/lual_error_safety_test.cc`

```cpp
struct TrackedResource {
    static int alive_count;
    TrackedResource() { alive_count++; }
    ~TrackedResource() { alive_count--; }
};

TEST(LuaLErrorSafety, RAIIDestructorCalled_BeforeErrorPath) {
    TrackedResource::alive_count = 0;
    {
        auto res = std::make_unique<TrackedResource>();
        EXPECT_EQ(TrackedResource::alive_count, 1);
        /* Trigger error path that would have bypassed destructor */
    }
    EXPECT_EQ(TrackedResource::alive_count, 0);
}

TEST(LuaLErrorSafety, BindFunction_CleanOnError) {
    ScriptVM vm;
    /* Invoke each binding function on an error path, verify ASAN clean */
    /* ... per-binding tests ... */
}
```

## Acceptance Criteria

1. All `luaL_error` call sites in 6 protocol binding files use the scope-block pattern
2. No RAII object with non-trivial destructor is alive on stack at any `luaL_error` call
3. `LuaError()` helper function is available in `bind_util.h`
4. All existing tests continue to pass
5. ASAN clean on error paths (no memory leaks)
6. Code review confirms the pattern is applied consistently

## Dependencies

- None

## Estimated Effort

- `LuaError` helper: ~20 lines
- Audit existing luaL_error calls: ~30 min manual review
- Fix 6 binding files: ~30 lines per file = ~180 lines
- Tests: ~100 lines
- **Total**: ~300 lines

## Risks

- **Subtle reintroduction**: The scope-block pattern relies on programmer discipline — easy to introduce new violations. Mitigation: document the pattern in `bind_util.h` and enforce in code review with a checklist item.
- **Error message truncation**: `LuaError` truncates to 512 bytes. For very long messages, use the scope-block pattern with the original `luaL_error` call.
- **Future fix**: Lua 5.5+ may introduce `lua_resetthread` + `lua_yield` as a non-longjmp error mechanism. Evaluate when available.
