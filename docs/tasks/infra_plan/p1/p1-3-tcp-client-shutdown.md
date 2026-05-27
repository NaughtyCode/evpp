# P1-3: TCP Client Explicit Shutdown

## Objective

Add explicit TCP client connection shutdown with global tracking (mirroring the server pattern), eliminating the current reliance on Lua GC for cleanup and fixing the asymmetric shutdown behavior.

## Current State

`ShutdownNetBindings()` (`net_bind.cc:75-86`) explicitly skips TCP client connections:

```cpp
void ShutdownNetBindings() {
    ShutdownHttpBindings();
    /* TCP client instances are cleaned up by Lua GC (__gc metamethod).
     * We do not maintain a global client map. */       /* <-- design gap */
    ShutdownServerBindings();
    ShutdownUdpServerBindings();
    ShutdownKcpServerBindings();
}
```

Unlike TCP Server, UDP Server, KCP Server, and HTTP bindings — which all have explicit Shutdown functions — TCP client connections rely entirely on Lua GC's `__gc` metamethod to trigger `Close()` and resource release.

## Root Cause

TCP clients have no global registry. `ClientCtx` is allocated via `new` in `l_client_connect()` and stored as lightuserdata. No map like `g_server_ctxs` tracks active client connections. The design assumes "client connections are ephemeral, managed by Lua script" — but this assumption fails during shutdown.

## Impact

1. **Shutdown resource leak**: Active TCP client connections aren't closed if Lua GC hasn't run — FDs, libevent resources, and memory persist until process exit
2. **Callback dangling risk**: `ClientCtx` callbacks capture raw `lua_State*` — if a queued network event fires after `ShutdownNetBindings` returns but before VM destruction, it accesses a partially cleaned Lua state
3. **Asymmetric with Server**: Inconsistency with all other network types that have explicit shutdown

## Implementation Steps

### Step 1: Add Global Client Context Map

**File**: `src/runtime/script/net_tcp_client_bind.cc`

```cpp
/* Mirror the server pattern — track all active client connections */
static std::mutex g_client_ctxs_mutex;
static std::unordered_map<evpp::TCPClient*, ClientCtx*> g_client_ctxs;

/* In l_client_connect(), after creating ctx: */
{
    std::lock_guard lock(g_client_ctxs_mutex);
    g_client_ctxs[ctx->client.get()] = ctx;
}
```

### Step 2: Implement ShutdownClientBindings

**File**: `src/runtime/script/net_tcp_client_bind.cc`

```cpp
void ShutdownClientBindings() {
    /* Step 1: Copy context list under lock */
    std::vector<ClientCtx*> ctxs;
    {
        std::lock_guard lock(g_client_ctxs_mutex);
        ctxs.reserve(g_client_ctxs.size());
        for (auto& [client, ctx] : g_client_ctxs) {
            ctxs.push_back(ctx);
        }
        g_client_ctxs.clear();
    }

    /* Step 2: For each client — dispose, close, unref */
    auto L = Engine::Instance().GetScriptVM().GetState();
    for (auto* ctx : ctxs) {
        ctx->disposed = true;                          /* prevent new callbacks */

        if (ctx->client && ctx->client->IsConnected()) {
            ctx->client->Close();                      /* close connection */
        }

        if (ctx->instance_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
            ctx->instance_ref = LUA_NOREF;
        }

        /* Clear callback captures to release shared_ptr references */
        ctx->client.reset();

        delete ctx;
    }
}
```

### Step 3: Update ShutdownNetBindings

**File**: `src/runtime/script/net_bind.cc`

```cpp
void ShutdownNetBindings() {
    ShutdownHttpBindings();
    ShutdownClientBindings();        /* NOW — was previously skipped */
    ShutdownServerBindings();
    ShutdownUdpServerBindings();
    ShutdownKcpServerBindings();
}
```

### Step 4: Add Lua API for Explicit Client Close

**File**: `src/runtime/script/net_tcp_client_bind.cc`

Ensure `client:close()` also removes from the global map:

```cpp
int l_client_close(lua_State* L) {
    auto* ctx = GetClientCtx(L, 1);
    if (ctx->disposed) return 0;

    ctx->disposed = true;
    ctx->client->Close();

    /* Remove from global map */
    {
        std::lock_guard lock(g_client_ctxs_mutex);
        g_client_ctxs.erase(ctx->client.get());
    }

    return 0;
}
```

### Step 5: Tests

**File**: `src/tests/unit/tcp_client_shutdown_test.cc`

- Create multiple clients, call ShutdownClientBindings, verify all are closed
- Verify closed clients are removed from global map
- Verify no use-after-free if a network event arrives after dispose
- Stress test: create → shutdown → create → shutdown cycles, verify no leaks (ASAN)

## Acceptance Criteria

1. `g_client_ctxs` global map tracks all active TCP client connections
2. `ShutdownClientBindings()` iterates all clients, sets disposed, calls Close(), unrefs, and deletes
3. `ShutdownNetBindings()` calls `ShutdownClientBindings()`
4. `client:close()` removes from global map
5. All client connections are properly closed on engine shutdown (verify via valgrind/ASAN)
6. Tests pass

## Dependencies

- P0-6 (RunInLoop Safety) — client callbacks need the NetAliveGuard pattern

## Estimated Effort

- Global map + mutex: ~15 lines
- ShutdownClientBindings: ~30 lines
- net_bind.cc change: 1 line
- close() map removal: ~10 lines
- Tests: ~100 lines
- **Total**: ~150 lines

## Risks

- **Thread safety**: `g_client_ctxs` is accessed from network event thread (connect/disconnect callbacks) and main thread (shutdown). The mutex protects the map, but `ctx->disposed` must be checked in all callbacks.
- **ClientCtx ownership**: After `Close()`, the underlying `TCPClient` may fire a deferred callback. Ensure the `disposed` flag is checked before any Lua state access in those callbacks.
