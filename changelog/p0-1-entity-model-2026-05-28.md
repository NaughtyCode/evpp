# P0-1: Entity Model — Entity/Actor/GameObject Abstraction Layer

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p0/p0-1-entity-model.md`

## Summary

Introduced a C++ `Entity` base class with a Component model, providing entity ID
allocation, attribute management, lifecycle state machine, automatic timer cleanup,
connection binding, and a Lua scripting interface. This fills the abstraction gap
between raw network connections and game business logic.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/entity/entity_id.h` | `EntityId` (uint64_t), `EntityIdAllocator` interface, `SequentialIdAllocator` |
| `src/runtime/entity/attribute.h` | `AttrValue` (variant<int64_t, double, string, bool>), `AttributeTable` with change notification |
| `src/runtime/entity/entity.h` | `EntityState` enum (Created/Active/Suspended/Destroyed), `Entity` class with components, timers, connection binding |
| `src/runtime/entity/entity.cc` | `Entity` lifecycle, timer ownership, connection binding implementations |
| `src/runtime/entity/entity_manager.h` | `EntityManager` singleton: create, lookup, destroy, iterate, connection→entity mapping |
| `src/runtime/entity/entity_manager.cc` | `EntityManager` singleton, ID allocation, connection registration implementations |
| `src/runtime/script/entity_bind.h` | `ExportEntity`/`ShutdownEntityBindings` declarations |
| `src/runtime/script/entity_bind.cc` | Lua binding: entity.create, get/set_attr, lifecycle, timers, components, connection delegation |
| `src/tests/unit/entity/test_entity.cpp` | 22 tests: ID allocation, attributes, lifecycle, components, timers, EntityManager |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/CMakeLists.txt` | Added `ENTITY_SOURCES` with entity files + `entity_bind.cc/.h` to script sources |
| `src/runtime/script/script_bind.h` | Declared `ExportEntity` and `ShutdownEntityBindings` |
| `src/runtime/script/script_bind.cc` | Added entity export call in `ExportAll` |
| `src/runtime/engine/engine.cc` | Added `ShutdownEntityBindings()` call in `Cleanup` (between net and timer shutdown) |
| `src/tests/unit/CMakeLists.txt` | Added `test_entity` target |

## Design

### Entity Lifecycle

```
Created → Active → Suspended → Destroyed
              ↑__________________|
```

- **Created**: Allocated, not yet active in the world
- **Active**: Fully active, receives updates and messages
- **Suspended**: Temporarily inactive (e.g., player disconnected, awaiting reconnect)
- **Destroyed**: All timers cancelled, connection unbound, components cleared

### Key Design Decisions

1. **EntityCtx stores EntityId, not Entity***: The Lua binding stores `EntityId` in
   the context, looking up the `Entity*` from `EntityManager` on each call. This
   avoids dangling pointer issues since `EntityManager` owns all entities.

2. **Timer safety via EntityId guard**: `Entity::AddTimer` wraps the callback to
   check entity existence and state before invoking. If the entity was destroyed,
   the timer callback safely no-ops.

3. **Connection binding via Lua delegation**: `entity:bind_connection(conn)` stores
   the Lua conn table ref; `entity:send(data)` calls `conn:send(data)` through Lua.
   The C++ `Entity::BindConnection(TCPConnPtr)` is available for programmatic use.

4. **Component storage via shared_ptr<void>**: Type-erased C++ components use
   `shared_ptr<void>` for automatic deleter management. Lua components are stored
   as registry refs with string names.

5. **Shutdown order**: Entity bindings shut down after net bindings (to release
   connection refs) but before timer bindings (entity-owned timers reference Lua).

## Acceptance Criteria

- [x] Entity can be created, activated, suspended, and destroyed
- [x] Entity destruction automatically cancels all owned timers
- [x] Attribute changes trigger registered callbacks
- [x] Connection binding/unbinding works correctly (Lua delegation + C++ API)
- [x] `FindByConnection()` returns the correct entity
- [x] EntityManager correctly tracks all entities (Count, ActiveCount, ForEachActive)
- [x] Lua binding exposes entity.create, lifecycle, attributes, timers, and components
- [x] 64-bit entity IDs are unique within a process (SequentialIdAllocator)
- [x] 22 unit tests pass (67 assertions)
- [x] All regression tests pass (sandbox: 115, scriptvm: 40, lual_error: 21, message_limits: 15, runinloop_safety: 18)
- [x] Build succeeds with no new warnings

## Deferred to Future Plans

- SnowflakeIdAllocator (distributed ID allocation)
- Auto-entity-on-connect integration with TCP server (P0-1 Step 6)
- Lua integration tests (entity + TCP server combined)
- Entity event listener system
