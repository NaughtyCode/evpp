# P1-1: Binding Boilerplate Elimination — Unify Network Binding Patterns

## Objective

Eliminate ~1500 lines of duplicated boilerplate across 6 network binding files by applying the `bind_util.h` template pattern (`GetUserdata<T>`/`NewUserdata<T>`) to the network binding layer. This replaces the manual lightuserdata + disposed + luaL_ref + __gc pattern with a unified, reusable template.

## Current State

Network bindings use a manual pattern duplicated across 6 files:

```cpp
/* Each binding manually implements: */
struct ConnCtx {
    evpp::TCPConnPtr conn;
    lua_State* L = nullptr;
    int instance_ref = LUA_NOREF;
    bool disposed = false;  /* prevents use-after-free */
};

/* In every function: */
auto* ctx = static_cast<ConnCtx*>(lua_touserdata(L, 1));
if (!ctx || ctx->disposed) return luaL_error(L, "disposed");

/* In __gc: */
ctx->disposed = true;
luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
loop->RunInLoop([del_ctx] { delete del_ctx; });
```

Scale of the duplication:

| Binding file | Lines | Context struct | luaL_ref/unref | disposed refs | __gc/__index=mt |
|-------------|-------|---------------|----------------|--------------|-----------------|
| net_tcp_server_bind.cc | 625 | ServerCtx + ConnCtx | 10 | 28 | 4 |
| net_tcp_client_bind.cc | 378 | ClientCtx | 4 | 17 | 5 |
| net_kcp_server_bind.cc | 441 | KcpServerCtx | 17 | 14 | 2 |
| net_kcp_client_bind.cc | ~340 | KcpClientCtx | ~10 | 14 | 2 |
| net_udp_server_bind.cc | ~420 | UdpServerCtx | 16 | 10 | 4 |
| net_udp_client_bind.cc | ~280 | UdpClientCtx | ~9 | 9 | 6 |
| **Total** | **~2500** | 6 structs | **60** | **92** | **30** |

~80% of the binding code is duplicated patterns.

MongoDB bindings (46 files) already use `src/runtime/database/mongo_bind/bind_util.h` templates (`GetUserdata<T>`/`NewUserdata<T>`) successfully. The network layer should adopt the same approach — either by moving `bind_util.h` to a shared location (`src/runtime/script/`) or creating a network-specific version there.

## Root Cause

Network bindings were written before the `bind_util.h` template pattern was established for MongoDB. The proven pattern was not retroactively applied to the network layer.

## Implementation Steps

### Step 1: Move/Extend bind_util.h for Network Contexts

**Existing file**: `src/runtime/database/mongo_bind/bind_util.h` (current location, MongoDB-specific)
**Target file**: `src/runtime/script/bind_util.h` (new shared location for both DB and network layers)

Add a generic Lua userdata wrapper for network context objects:

```cpp
/*
 * LuaUserdata<T> — RAII wrapper for C++ objects exposed to Lua as full userdata.
 *
 * Replaces the manual lightuserdata + disposed + luaL_ref + __gc pattern.
 *
 * Usage:
 *   1. Define: struct MyCtx { ... };
 *   2. In init function: NewLuaUserdata<MyCtx>(L, "MyCtx", new MyCtx(...));
 *   3. In every method: auto* ctx = GetLuaUserdata<MyCtx>(L, 1);
 *   4. __gc, __index, disposed checking are automatic.
 */
template<typename T>
struct LuaUserdata {
    T* ptr;
    int instance_ref = LUA_NOREF;  /* ref to the Lua wrapper table */
    bool disposed = false;
};

template<typename T>
T* GetLuaUserdata(lua_State* L, int index) {
    auto* ud = static_cast<LuaUserdata<T>*>(
        luaL_checkudata(L, index, T::kMetatableName));
    if (ud->disposed) {
        luaL_error(L, "%s: object has been disposed", T::kMetatableName);
        return nullptr;  /* unreachable */
    }
    return ud->ptr;
}

template<typename T>
LuaUserdata<T>* NewLuaUserdata(lua_State* L, const char* metatable_name,
                                T* ptr, int instance_table_ref);

template<typename T>
void RegisterLuaUserdataMeta(lua_State* L, const char* metatable_name);
```

### Step 2: Create a Code Generation Script (Optional)

If the template approach still requires per-type boilerplate, create a code generator:

**File**: `tools/gen_net_binding.py`

```python
"""Generate network binding boilerplate from a type definition."""
types = [
    {"name": "TcpServer", "ctx": "ServerCtx", "methods": ["listen", "stop", ...]},
    {"name": "TcpConnection", "ctx": "ConnCtx", "methods": ["send", "close", ...]},
    # ...
]
# Generates: metatable registration, __gc, __index, method stubs
```

### Step 3: Refactor TCP Server Binding

**File**: `src/runtime/script/net_tcp_server_bind.cc`

Replace manual lightuserdata handling with the template:

```cpp
/* Register metatable once: */
RegisterLuaUserdataMeta<ServerCtx>(L, "TcpServer");
RegisterLuaUserdataMeta<ConnCtx>(L, "TcpConnection");

/* Create context: */
auto* ud = NewLuaUserdata<ConnCtx>(L, "TcpConnection", new ConnCtx(...), inst_ref);

/* In methods: */
auto* ctx = GetLuaUserdata<ConnCtx>(L, 1);  /* auto checks disposed */
```

### Step 4: Refactor TCP Client Binding

**File**: `src/runtime/script/net_tcp_client_bind.cc`

Same template pattern.

### Step 5: Refactor KCP Server Binding

**File**: `src/runtime/script/net_kcp_server_bind.cc`

Same template pattern.

### Step 6: Refactor KCP Client Binding

**File**: `src/runtime/script/net_kcp_client_bind.cc`

Same template pattern.

### Step 7: Refactor UDP Server Binding

**File**: `src/runtime/script/net_udp_server_bind.cc`

Same template pattern.

### Step 8: Refactor UDP Client Binding

**File**: `src/runtime/script/net_udp_client_bind.cc`

Same template pattern.

### Step 9: Tests

**File**: `src/tests/unit/bind_util_network_test.cc`

- Create LuaUserdata, call methods, verify disposed check
- Trigger __gc, verify ptr is deleted and disposed flag set
- Use-after-dispose: verify luaL_error is raised
- Stress test: create/destroy 1000 userdata objects, verify no leaks

## Acceptance Criteria

1. `LuaUserdata<T>` template is implemented in `bind_util.h`
2. All 6 network binding files use the template instead of manual lightuserdata
3. 60 `luaL_ref`/`luaL_unref` calls eliminated (~80% reduction)
4. 92 `disposed` references eliminated (handled by template)
5. 30 `__gc`/`__index = mt` registrations eliminated
6. All existing network tests pass
7. New LuaUserdata tests pass
8. Code review confirms consistent pattern application

## Dependencies

- P0-4 (luaL_error Safety) — ensures template methods don't introduce new longjmp issues
- P0-6 (RunInLoop Safety) — template must integrate with the NetAliveGuard pattern

## Estimated Effort

- `LuaUserdata<T>` template: ~120 lines
- Refactor 6 binding files: ~100 lines replaced per file = ~600 lines changed
- Code generator (optional): ~100 lines
- Tests: ~100 lines
- **Total**: ~800 lines changed

## Risks

- **Full userdata vs lightuserdata**: Full userdata has slightly more overhead (malloc per userdata) than lightuserdata (just a pointer). For high-frequency connection create/destroy scenarios, benchmark the impact. For typical game server usage, the overhead is negligible.
- **Migration effort**: All 6 files need coordinated changes. Do one file first as a model, get it reviewed, then apply to the other 5.
- **Template complexity**: Debugging template errors can be harder than the simple manual pattern. Mitigation: keep the template simple and well-documented.
