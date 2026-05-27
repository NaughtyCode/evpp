# P1-5: Physics Result Integration — Connect Physics Output to Game Layer

## Objective

Wire up the physics simulation results (currently fetched but discarded) to the game object layer, so physics transforms are propagated to entity positions and available for network sync to clients.

## Current State

`engine.cc:432-436` explicitly comments out the physics result integration:

```cpp
/* engine.cc:432-436 */
/* Fetch physics results — currently discarded */
// auto physics_results = PhysicsEngineBridge::Instance().FetchResult();
// if (physics_results.has_value()) {
//     /* Apply transforms to game objects — "would go here" */
// }
```

`PhysicsEngineBridge` computes transforms, collision events, and diff packets in the physics thread, transfers them via SPSC queue to the main thread, but the main thread fetches and discards them. The physics pipeline is fully functional but its output is disconnected.

## Root Cause

No entity model existed to receive physics results (the abstraction gap described in P0-1). Connecting physics results to "game objects" requires a game object to exist first.

## Implementation Steps

### Step 1: Define Physics Result Handler Interface

**File**: `src/runtime/engine/engine.h`

```cpp
/* Callback invoked after physics results are fetched each frame */
using PhysicsResultHandler = std::function<void(const PhysicsDiffPacket&)>;

class Engine {
public:
    void SetPhysicsResultHandler(PhysicsResultHandler handler);
    /* ... */
};
```

### Step 2: Wire FetchResult → Entity Updates

**File**: `src/runtime/engine/engine.cc` (~line 432)

```cpp
/* In Engine::Update() — after FetchResult: */
auto physics_results = PhysicsEngineBridge::Instance().FetchResult();
if (physics_results.has_value()) {
    auto& [transforms, collision_events] = *physics_results;

    for (auto& transform : transforms) {
        Entity* entity = EntityManager::Instance().FindByPhysicsBodyId(transform.body_id);
        if (entity) {
            entity->Attrs().Set("position_x", transform.position.x);
            entity->Attrs().Set("position_y", transform.position.y);
            entity->Attrs().Set("position_z", transform.position.z);
            entity->Attrs().Set("rotation_w", transform.rotation.w);
            entity->Attrs().Set("rotation_x", transform.rotation.x);
            entity->Attrs().Set("rotation_y", transform.rotation.y);
            entity->Attrs().Set("rotation_z", transform.rotation.z);
        }
    }

    for (auto& event : collision_events) {
        LuaCallbackManager::DispatchCollisionEvent(event.entity_a, event.entity_b, event.contact);
    }
}
```

### Step 3: Add Entity-PhysicsBody Linkage

**File**: `src/runtime/entity/entity.h`

Add a field to link an Entity to its Physics body:

```cpp
class Entity {
    /* ... */
    void SetPhysicsBodyId(uint32_t body_id) { physics_body_id_ = body_id; }
    uint32_t GetPhysicsBodyId() const { return physics_body_id_; }
    bool HasPhysicsBody() const { return physics_body_id_ != kInvalidBodyId; }

private:
    static constexpr uint32_t kInvalidBodyId = UINT32_MAX;
    uint32_t physics_body_id_ = kInvalidBodyId;
};
```

### Step 4: Lua Binding for Collision Events

**File**: `src/runtime/script/physics_bind.cc`

```cpp
/* Expose collision event handling to Lua */
int l_entity_on_collision(lua_State* L) {
    auto* entity = GetLuaUserdata<Entity>(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    LuaCallbackManager::RegisterCollisionCallback(entity->GetId(), [L, callback_ref](
        EntityId other, const ContactInfo& contact) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
        /* push other entity, contact info */
        lua_pushinteger(L, other);
        /* push contact table */
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            ENGINE_LOG_ERROR("Collision callback error: {}", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    });

    return 0;
}
```

### Step 5: Tests

**File**: `src/tests/integration/physics_integration_test.cc`

- Create entity, spawn physics body, run simulation, verify entity position updates
- Create two entities with colliding bodies, verify collision event fires
- Destroy entity, verify physics body is removed

## Acceptance Criteria

1. `Engine::Update()` applies physics transforms to Entity attributes
2. Entity-physics body linkage via `physics_body_id_`
3. Collision events dispatched to Lua callbacks
4. Physics results are no longer discarded
5. Tests verify end-to-end: physics simulation → entity position update

## Dependencies

- P0-1 (Entity Model) — requires entities to exist before physics results can be attached
- P1-4 (Physics CV Wakeup) — beneficial but not strictly required

## Estimated Effort

- Engine integration: ~40 lines
- Entity physics body linkage: ~20 lines
- Lua collision binding: ~50 lines
- Tests: ~150 lines
- **Total**: ~250 lines

## Risks

- **Performance**: Applying physics transforms to every entity every frame may be expensive for large worlds. Mitigation: only update entities that changed (diff-based updates).
- **Coordinate system mismatch**: Jolt Physics uses a different coordinate system than the game world. Ensure consistent transform application.
- **Concurrency**: FetchResult is called from the main thread; physics runs in a separate thread. The SPSC queue handles this, but verify no additional synchronization issues.
