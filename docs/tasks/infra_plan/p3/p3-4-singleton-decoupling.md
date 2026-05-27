# P3-4: Singleton Decoupling — Enable Multi-Engine Instances

## Objective

Decouple the 8 singleton classes so that multiple Engine instances can run independently in the same process, and subsystems can be tested in isolation.

## Current State

8 singleton classes with 80+ `.Instance()` calls:

| Singleton | Instances |
|-----------|-----------|
| Engine | 80+ calls throughout codebase |
| ConfigManager | subsystem initialization |
| TimerManager | bindings, tests |
| ScriptVM | bindings (via Engine::GetScriptVM) |
| DatabaseService | bindings, DB thread |
| PhysicsSystem | PhysicsEngineBridge |
| PhysicsEngineBridge | engine.cc |
| EntityManager | (after P0-1 implementation) |

All bindings hard-depend on `Engine::Instance().GetScriptVM().GetState()`.

## Root Cause

Convenience pattern for single-process game servers. The assumption that "one process = one engine instance" made singleton acceptable during prototyping.

## Impact

- Cannot run multiple Engine instances in one process (e.g., integration tests with multiple independent game worlds)
- Cannot isolate Space/Room configurations
- All bindings have hidden dependency on global Engine state

## Implementation Steps

### Step 1: Pass Dependencies Explicitly

Replace singleton access with explicit dependency injection:

```cpp
/* Before: */
int l_conn_send(lua_State* L) {
    auto* ctx = static_cast<ConnCtx*>(lua_touserdata(L, 1));
    auto* engine_loop = Engine::Instance().GetMainLoop();
    /* ... */
}

/* After: */
int l_conn_send(lua_State* L) {
    auto* ctx = static_cast<ConnCtx*>(lua_touserdata(L, 1));
    /* EventLoop stored in ConnCtx — arrived via constructor injection */
    auto* engine_loop = ctx->event_loop;
    /* ... */
}
```

### Step 2: Store Engine Context in Lua Registry

```cpp
/* Each Lua state has its owning Engine pointer in the registry */
static constexpr int kEngineRegistryKey = 1;

void SetEngineForLuaState(lua_State* L, Engine* engine) {
    lua_pushlightuserdata(L, engine);
    lua_rawseti(L, LUA_REGISTRYINDEX, kEngineRegistryKey);
}

Engine* GetEngineForLuaState(lua_State* L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, kEngineRegistryKey);
    auto* engine = static_cast<Engine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}
```

### Step 3: Remove PhysicsEngineBridge Singleton

Absorb into `Engine` via PIMPL:

```cpp
/* engine.h */
class Engine {
    /* ... */
    std::unique_ptr<PhysicsEngineBridge> physics_bridge_;  /* not a singleton */
};
```

### Step 4: Phased Migration

1. Start with new code: all new subsystems use dependency injection
2. Phase 2: migrate `TimerManager`, `ConfigManager` (low impact)
3. Phase 3: migrate `DatabaseService` (medium impact)
4. Phase 4: migrate `Engine` (highest impact — 80+ call sites)

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/singleton_decouple_test.cc`:
- Two Engine instances in same process → each has independent TimerManager, ConfigManager
- No cross-contamination between engines: timer created on Engine1 does not fire on Engine2
- Per-VM Engine context correctly retrieved from Lua (each VM sees its own Engine)
- Existing single-Engine usage unchanged (backward compatibility)

```
src/tests/unit/singleton_decouple_test.cc   # ~60 lines
```

## Acceptance Criteria

1. All new subsystems use dependency injection
2. `PhysicsEngineBridge` is no longer a singleton
3. Engine context is stored per Lua state (not global)
4. Multi-Engine test: 2 engines in same process, independent operation
5. Backward compatibility: existing single-Engine usage unchanged

## Dependencies: None (long-term refactor, independent of other plans)

## Estimated Effort: ~500 lines (phased over multiple releases)
