# P1-9: Entity-Connection Binding — Multi-VM Architecture

## Objective

Implement a multi-VM architecture where each game space/room runs in its own Lua VM, with entity-to-connection binding managed by the engine. This reuses the proven patterns from Physics (independent VM per physics thread) and DBThread (per-thread VM).

## Current State

All Lua scripts share a single global `lua_State` (`ScriptVM` in `engine.cc`). The Physics and Database subsystems have their own independent VMs (`PhysicsScriptVM` and per-DBThread VMs), demonstrating that multi-VM architecture is feasible. Business logic, however, is confined to a single VM — preventing space/room-level isolation.

## Root Cause

Single-VM design from the prototype phase. Multi-VM requires:
1. Lua state per space/room
2. Cross-VM message passing
3. Entity ownership tied to a specific VM
4. Connection → VM routing

## Implementation Steps

### Step 1: Define Space (VM Instance) Abstraction

**File**: `src/runtime/space/space.h`

```cpp
/*
 * Space represents an isolated game world with its own Lua VM.
 *
 * Each Space has:
 *   - An independent lua_State (isolated global namespace)
 *   - An Entity set (entities owned by this space)
 *   - A Physics world (optional, for spatially partitioned spaces)
 *   - A TimerManager (per-space timers)
 *
 * Spaces communicate via a message passing interface (see Step 3).
 */
class Space {
public:
    explicit Space(SpaceId id, const SpaceConfig& config);
    ~Space();

    SpaceId GetId() const { return id_; }
    lua_State* GetLuaState() { return L_; }

    /* Entity management within this space */
    Entity* CreateEntity(EntityId id = 0);
    Entity* GetEntity(EntityId id);
    void DestroyEntity(EntityId id);

    /* Player join/leave */
    void OnPlayerJoin(EntityId player_id, evpp::TCPConnPtr conn);
    void OnPlayerLeave(EntityId player_id);

    /* Per-frame update */
    void Update(int64_t delta_ms);

    /* Script loading */
    bool LoadScripts(const std::vector<std::string>& script_paths);

private:
    SpaceId id_;
    lua_State* L_ = nullptr;
    std::unique_ptr<EntityManager> entity_manager_;
    std::unique_ptr<TimerManager> timer_manager_;
    std::unordered_map<EntityId, evpp::TCPConnPtr> player_connections_;
};
```

### Step 2: Space Manager

**File**: `src/runtime/space/space_manager.h`

```cpp
/*
 * SpaceManager manages all active Spaces.
 *
 * Responsibilities:
 *   - Create/destroy spaces
 *   - Route incoming connections/messages to the correct space
 *   - Manage space lifecycle (create → active → draining → destroyed)
 *   - Enforce per-space resource limits
 */
class SpaceManager {
public:
    Space* CreateSpace(const SpaceConfig& config);
    Space* GetSpace(SpaceId id);
    void DestroySpace(SpaceId id);

    /*
     * Route an incoming message to the appropriate space.
     * Looks up SpaceId from the connection → entity → space chain.
     */
    void RouteMessage(EntityId target_entity, const std::string& message);

    /*
     * Transfer an entity from one space to another.
     */
    bool TransferEntity(EntityId entity_id, SpaceId from, SpaceId to);

    size_t SpaceCount() const;

private:
    std::unordered_map<SpaceId, std::unique_ptr<Space>> spaces_;
};
```

### Step 3: Cross-Space Message Passing

**File**: `src/runtime/space/space_message.h`

```cpp
/*
 * Cross-space message passing.
 *
 * Spaces communicate via async messages. Messages are serialized
 * (using msgpack or JSON) to pass across VM boundaries.
 *
 * Lua API:
 *   space.send(space_id, entity_id, message)
 *   entity.on_message = function(entity, from_entity_id, message) ... end
 */
struct SpaceMessage {
    SpaceId source_space;
    SpaceId target_space;
    EntityId source_entity;
    EntityId target_entity;
    std::string payload;  /* serialized message data */
};

class SpaceMessageRouter {
public:
    void SendMessage(SpaceMessage msg);
    void ProcessPending();  /* called each frame */

private:
    moodycamel::ConcurrentQueue<SpaceMessage> pending_messages_;
};
```

### Step 4: Connection Routing

**File**: `src/runtime/space/connection_router.h`

```cpp
/*
 * Routes incoming TCP connections to the appropriate Space.
 *
 * On connect:
 *   1. New TCP connection arrives
 *   2. Authentication (if enabled) identifies the player
 *   3. Player is assigned to a Space (load-balanced or based on world location)
 *   4. Entity is created or reconnected in the target Space
 *   5. Connection messages are routed to that Space's Lua VM
 */
class ConnectionRouter {
public:
    /*
     * Handle a new connection. Returns the assigned Space.
     */
    Space* RouteNewConnection(evpp::TCPConnPtr conn);

    /*
     * Route incoming data from a connection to its Space.
     */
    void RouteMessage(evpp::TCPConnPtr conn, const std::string& data);

    /*
     * Handle connection close — notify the Space (entity suspend, not destroy).
     */
    void RouteDisconnection(evpp::TCPConnPtr conn);

private:
    /* connection → space mapping */
    std::unordered_map<evpp::TCPConnPtr, SpaceId> conn_to_space_;
};
```

### Step 5: Engine Integration

**File**: `src/runtime/engine/engine.cc`

```cpp
/* In Engine::Init() */
SpaceManager::Instance().Init();

/* In Engine::Start() */
/* Create default space (backward compatible — single space = single VM) */
auto* default_space = SpaceManager::Instance().CreateSpace(SpaceConfig{
    .name = "default",
    .entry_scripts = config_.entry_scripts,
    .max_entities = config_.max_entities_per_space,
});

/* In Engine::Update() — update all spaces */
SpaceManager::Instance().ForEachSpace([delta_ms](Space& space) {
    space.Update(delta_ms);
});
SpaceMessageRouter::Instance().ProcessPending();
```

### Step 6: Lua API

**File**: `src/runtime/script/space_bind.cc`

```lua
-- Create a new space
local dungeon = space.create("dungeon_01", {
    max_players = 5,
    scripts = {"dungeon/main.lua"}
})

-- Send a message to an entity in another space
space.send(dungeon.id, target_entity_id, {type = "damage", amount = 50})

-- Transfer a player to another space
entity:transfer_to(dungeon.id)

-- Get current space info
local info = space.current()
print(info.name, info.id, info.player_count)
```

### Step 7: Tests

**File**: `src/tests/unit/space_test.cc`

- Create space, load scripts, verify independent VM
- Create entity in space, verify entity is in space's EntityManager
- Cross-space message: send from Space A entity to Space B entity
- Destroy space, verify all entities cleaned up
- Transfer entity between spaces

**File**: `src/tests/integration/multi_vm_test.cc`

- Two spaces with different scripts, verify no cross-contamination
- 100 spaces created and destroyed, verify no memory leaks (ASAN)

## Acceptance Criteria

1. `Space` abstraction with independent Lua VM per space
2. `SpaceManager` creates/destroys spaces and routes messages
3. `SpaceMessageRouter` delivers cross-space messages via SPSC queue
4. `ConnectionRouter` assigns new connections to spaces
5. Player disconnect suspends entity (not destroys — enables reconnect)
6. Single-space mode is backward compatible (default space = current behavior)
7. No cross-VM global variable contamination
8. Tests verify isolation, routing, transfer, and cleanup

## Dependencies

- P0-1 (Entity Model) — entities are the core objects managed by spaces
- P0-6 (RunInLoop Safety) — cross-VM message delivery needs safe dispatch

## Estimated Effort

- Space class: ~150 lines
- SpaceManager: ~80 lines
- SpaceMessageRouter: ~60 lines
- ConnectionRouter: ~80 lines
- Engine integration: ~40 lines
- Lua binding: ~80 lines
- Tests: ~200 lines C++ + ~100 lines Lua
- **Total**: ~790 lines

## Risks

- **Memory overhead**: Each Lua VM has memory overhead (~50-100KB empty). With 1000 spaces, that's 50-100MB. Mitigation: lazy VM creation, shared read-only metatables where possible.
- **Cross-VM complexity**: Debugging across multiple VMs is harder than a single VM. Add SpaceId to all log messages.
- **Message serialization overhead**: Cross-space messages are serialized/deserialized. For high-frequency messages, consider a shared-memory approach (but this breaks VM isolation).
- **Garbage collection coordination**: Multiple VMs means multiple GC cycles. Consider coordinating GC to avoid frame time spikes.
