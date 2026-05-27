# P0-6: Cross-thread RunInLoop Lifetime Safety — lua_State Dangling Pointer

## Objective

Eliminate the use-after-free risk in KCP and UDP bindings where `RunInLoop` callbacks capture raw `lua_State*` pointers. Generalize the HTTP binding's proven `g_net_alive` + mutex + pending refs pattern to all cross-thread callback scenarios.

## Current State

KCP and UDP binding layers dispatch network messages from receive threads to the main thread via `RunInLoop`, capturing raw `lua_State*`:

```cpp
/* net_kcp_server_bind.cc:58-91 */
ctx->server->SetMessageHandler([ctx, main_loop](evpp::EventLoop*, evpp::kcp::MessagePtr& msg) {
    lua_State* L_ptr = ctx->L;  /* captures raw pointer */
    main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip, conv]() {
        if (!L_ptr || msg_ref == LUA_NOREF) return;  /* insufficient guard */
        lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);  /* L_ptr may be dangling */
    });
});
```

Same pattern in: `net_udp_server_bind.cc` and `net_kcp_server_bind.cc`.

Affected files with raw pointer capture in RunInLoop:

| File | # RunInLoop calls | Captured pointers |
|------|-------------------|-------------------|
| `net_kcp_server_bind.cc` | 4 (lines 73, 112, 280, 409) | L_ptr, ctx, L, old_msg_ref, old_inst_ref |
| `net_udp_server_bind.cc` | 4 (lines 79, 124, 298, 381) | L_ptr, ctx, L, old_msg_ref, old_inst_ref |
| `net_tcp_server_bind.cc` | 5 (lines 183, 246, 289, 356, 491) | del_ctx, ctx |
| `net_tcp_client_bind.cc` | 3 (lines 171, 244, 294) | del_ctx, ctx_ptr |
| **Total** | **16+** | Raw pointers across 4 files |

## Root Cause

1. **No explicit happens-before ordering** between Shutdown (which sets disposed flags) and VM destruction. The implicit ordering in `Engine::Cleanup` is fragile and undocumented.
2. **`g_net_alive` pattern not generalized**: HTTP binding (`net_http_bind.cc`) has a proven solution using `g_net_alive` atomic + mutex + pending ref list + TOCTOU-safe shutdown. This pattern was not applied to KCP/UDP.
3. **`RunInLoop` delayed delete assumption**: Binding code assumes `loop->RunInLoop([del_ctx]{ delete del_ctx; })` is safe because the loop is still running — but during Shutdown, the loop may stop before the callback executes.

## Impact

- **Use-After-Free**: `lua_rawgeti` on a freed `lua_State` causes SIGSEGV
- **Non-deterministic crashes**: Depends on GC timing, event loop drain speed, thread scheduling
- **Release-only crashes**: Hard to reproduce and debug

## Implementation Steps

### Step 1: Create a Generalized NetAlive Guard

**File**: `src/runtime/script/net_lifetime.h`

```cpp
/*
 * NetAliveGuard — generalized version of HTTP binding's g_net_alive pattern.
 *
 * Usage:
 *   1. Each network module declares: static NetAliveGuard g_xxx_alive;
 *   2. Before RunInLoop lambda captures, check g_xxx_alive.IsAlive().
 *   3. Shutdown: g_xxx_alive.Shutdown() → drain event loop → cleanup.
 */
class NetAliveGuard {
public:
    bool IsAlive() const { return alive_.load(std::memory_order_acquire); }

    /*
     * Shutdown: atomically set alive=false, then wait for all in-flight
     * callbacks to drain by taking the mutex (callbacks hold it during
     * Lua operations).
     */
    void Shutdown();

    /*
     * Acquire a "running" slot. Returns true if still alive and the
     * callback should proceed. Must call Release() when done.
     */
    bool TryAcquire();

    /*
     * Release the running slot.
     */
    void Release();

    /*
     * Block until all acquired callbacks have released.
     */
    void WaitDrain();

private:
    std::atomic<bool> alive_{true};
    std::mutex mutex_;
    int pending_count_ = 0;
};
```

### Step 2: Generalize Pending Refs Tracking

**File**: `src/runtime/script/net_lifetime.h`

```cpp
/*
 * PendingRefTracker — tracks Lua registry references held by in-flight
 * callbacks. During shutdown, all tracked refs are safely unref'd under
 * the mutex (preventing TOCTOU races with in-flight callbacks).
 */
class PendingRefTracker {
public:
    void AddRef(int ref);
    void RemoveRef(int ref);
    void UnrefAll(lua_State* L);  /* called during shutdown, under mutex */

private:
    std::vector<int> pending_refs_;  /* or std::unordered_set for O(1) */
    std::mutex mutex_;
};
```

### Step 3: Apply to KCP Server Binding

**File**: `src/runtime/script/net_kcp_server_bind.cc`

Replace the raw `lua_State*` capture pattern:

```cpp
/* BEFORE: */
static std::atomic<bool> g_kcp_alive{true};  /* exists but incomplete */

main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip, conv]() {
    if (!L_ptr || msg_ref == LUA_NOREF) return;
    lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);
    /* ... */
});

/* AFTER: */
static NetAliveGuard g_kcp_alive;
static PendingRefTracker g_kcp_pending;

main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip, conv]() {
    if (!g_kcp_alive.TryAcquire()) return;  /* shutdown in progress */
    if (msg_ref == LUA_NOREF) {
        g_kcp_alive.Release();
        return;
    }
    g_kcp_pending.AddRef(msg_ref);

    lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);
    /* ... process message ... */
    lua_pop(L_ptr, 1);  /* pop the function */

    g_kcp_pending.RemoveRef(msg_ref);
    g_kcp_alive.Release();
});
```

### Step 4: Apply to UDP Server Binding

**File**: `src/runtime/script/net_udp_server_bind.cc`

Same pattern as Step 3.

### Step 5: Implement ShutdownKcpServerBindings with Drain

**File**: `src/runtime/script/net_bind.cc` (or `net_kcp_server_bind.cc`)

```cpp
void ShutdownKcpServerBindings() {
    /* Step 1: Atomically mark as dead — no new callbacks will be acquired */
    g_kcp_alive.Shutdown();

    /* Step 2: Wait for all in-flight callbacks to finish */
    g_kcp_alive.WaitDrain();

    /* Step 3: Unref all remaining Lua references (callbacks that completed
     * but whose refs weren't cleaned up) */
    g_kcp_pending.UnrefAll(Engine::Instance().GetScriptVM().GetState());

    /* Step 4: Now safe to destroy KCP server objects */
    for (auto& ctx : g_kcp_server_ctxs) {
        ctx->server->Stop();
        /* ... */
    }
}
```

### Step 6: Apply to UDP Shutdown

Same drain pattern as Step 5 for UDP server binding.

### Step 7: Apply to TCP Client Shutdown

Related to P1-3 (TCP Client Explicit Shutdown). Apply the same guard pattern.

### Step 8: Document the Pattern

Add to `src/runtime/script/README.md` or inline comments: the `NetAliveGuard` + `PendingRefTracker` pattern must be used for any cross-thread callback that captures a `lua_State*` or Lua registry reference.

### Step 9: Tests

**File**: `src/tests/unit/runinloop_safety_test.cc`

- Create a KCP server, send a message, immediately shut down — verify no crash
- Stress test: rapid create/destroy cycles with messages in flight
- ASAN/valgrind verification for use-after-free

## Acceptance Criteria

1. `NetAliveGuard` class is implemented and reusable across network modules
2. `PendingRefTracker` class is implemented and reusable
3. KCP server binding uses `NetAliveGuard` + `PendingRefTracker`
4. UDP server binding uses `NetAliveGuard` + `PendingRefTracker`
5. Shutdown functions properly drain in-flight callbacks before releasing resources
6. All existing network tests pass
7. ASAN/valgrind clean during shutdown with in-flight messages
8. Pattern is documented for future network module authors

## Dependencies

- None

## Estimated Effort

- `NetAliveGuard` + `PendingRefTracker`: ~100 lines
- KCP binding refactor: ~60 lines
- UDP binding refactor: ~60 lines
- Shutdown drain logic: ~40 lines
- KCP client (if applicable): ~30 lines
- Tests: ~100 lines
- **Total**: ~400 lines

## Risks

- **Performance**: `TryAcquire()`/`Release()` adds atomic operations to every message dispatch (hot path). Measured overhead should be negligible (<1%) for the atomic acquire, but benchmark after implementation.
- **Deadlock**: `Shutdown()` must NOT be called from within a `TryAcquire()`/`Release()` scope. Document this constraint.
- **PendingRefTracker vector O(n) removal**: Use `std::unordered_set` for O(1) remove if pending refs per cycle is large (>10). See P2-16 for HTTP-specific optimization.
