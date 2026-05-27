# P2-11: RunInLoop — weak_ptr/shared_ptr Migration from Delayed Delete

## Objective

Replace the `RunInLoop([del_ctx]{ delete del_ctx; })` delayed-delete pattern with `std::shared_ptr`/`std::weak_ptr` based lifetime management, eliminating the assumption that the event loop is still running during Shutdown.

## Current State

Binding code uses a "delayed delete" pattern:

```cpp
/* Context allocated with new: */
auto* ctx = new ConnCtx();

/* Context freed via RunInLoop: */
loop->RunInLoop([del_ctx] { delete del_ctx; });

/* Problem: if loop is stopped/joined, the lambda never runs → ctx leaks */
/* Problem: during Shutdown, loop might stop before lambda executes */
```

This pattern appears ~15 times across 4 binding files.

## Implementation Steps

### Step 1: Convert Context to shared_ptr

**File**: `src/runtime/script/net_tcp_server_bind.cc`

```cpp
/* Before: */
auto* ctx = new ConnCtx();
ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);
/* Store lightuserdata in Lua */
lua_pushlightuserdata(L, ctx);
/* ... */
/* In __gc: */
loop->RunInLoop([del_ctx] { delete del_ctx; });

/* After: */
auto ctx = std::make_shared<ConnCtx>();
ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);
/* Store shared_ptr in Lua full userdata (via LuaUserdata template from P1-1) */
NewLuaUserdata<ConnCtx>(L, "TcpConnection", std::move(ctx), inst_ref);
/* ... */
/* In __gc: */
/* shared_ptr ref count drops; if last reference, ConnCtx is destroyed immediately */
/* No RunInLoop delay needed */
```

### Step 2: weak_ptr for Cross-Thread Callbacks

```cpp
/* For RunInLoop callbacks that capture context from another thread: */
std::weak_ptr<ConnCtx> weak_ctx = ctx;

main_loop->RunInLoop([weak_ctx, L]() {
    auto ctx = weak_ctx.lock();
    if (!ctx || ctx->disposed) return;  /* context already destroyed */
    /* Safe to use ctx */
});
```

### Step 3: Apply to All 4 Binding Files

- `net_tcp_server_bind.cc`
- `net_tcp_client_bind.cc`
- `net_kcp_server_bind.cc`
- `net_udp_server_bind.cc`

### Step 4: Tests

- Create context, trigger __gc, verify context destroyed (ref count drops to 0)
- Cross-thread callback with weak_ptr: destroy context, verify callback safely returns
- No leaks under ASAN

## Acceptance Criteria

1. All binding context objects managed by `shared_ptr`
2. All `RunInLoop` callbacks use `weak_ptr` for captured contexts
3. No `RunInLoop([del_ctx]{ delete del_ctx; })` patterns remain
4. Context destruction is deterministic (ref count reaches 0)
5. Shutdown does not leak contexts
6. ASAN clean

## Dependencies

- P1-1 (Binding Boilerplate) — template approach enables easier shared_ptr migration
- P0-6 (RunInLoop Safety) — weak_ptr complements NetAliveGuard

## Estimated Effort: ~300 lines
