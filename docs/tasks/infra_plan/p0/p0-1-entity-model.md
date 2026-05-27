# P0-1: Entity Model — Entity/Actor/GameObject Abstraction Layer

## Objective

Introduce a C++ `Entity` base class with Component model, providing entity ID allocation, attribute management, lifecycle state machine, automatic resource cleanup, and connection binding/unbinding. This fills the abstraction gap between raw network connections and game business logic.

## Current State

The highest abstraction exposed to Lua is **TCP Connection** and **Timer**. There is no entity storage of any kind. Developers work with raw byte streams on connections:

```lua
server.on_connect = function(conn)
    conn.on_message = function(conn, data)
        -- data is raw bytes; no entity concept exists
        conn:send("reply")
    end
end
```

`VMCustomPtrStore` (`src/runtime/vm/custom_ptr_store.h`) stores C++ subsystem pointers for dependency injection, not game entities.

Missing capabilities that are infrastructure (not business logic):
- Entity ID allocation (snowflake / segmented ID)
- Entity attribute management (read/write/change notification)
- Entity lifecycle state machine (create/activate/suspend/destroy)
- Entity-to-entity messaging (with offline caching)
- Entity-connection binding (login = create+bind; disconnect = unbind but retain for reconnect)

## Root Cause

The architecture has a massive abstraction gap between "network connection" and "game entity," forcing every game project to reinvent entity management in Lua, resulting in ~90% glue code.

## Impact

For a medium-scale game (5000 online, 10 NPC types, 100 item types):
- 200-500 lines of manual create/destroy/attribute code per entity type
- Manual weak reference management to prevent Lua GC cycles
- Manual timer/event listener cancellation on entity destruction
- Manual per-field serialization/deserialization

## Implementation Steps

### Step 1: Define Entity ID System

**File**: `src/runtime/entity/entity_id.h`

```cpp
/* 64-bit entity ID supporting both snowflake and segmented allocation strategies */
using EntityId = uint64_t;

/* ID allocator interface — allows swapping allocation strategy */
class EntityIdAllocator {
public:
    virtual ~EntityIdAllocator() = default;
    virtual EntityId Allocate() = 0;
    virtual void Release(EntityId id) = 0;
};

/* Default snowflake-style allocator */
class SnowflakeIdAllocator : public EntityIdAllocator { ... };

/* Simple sequential allocator for single-process deployments */
class SequentialIdAllocator : public EntityIdAllocator { ... };
```

### Step 2: Define Attribute System

**File**: `src/runtime/entity/attribute.h`

```cpp
/* Supported attribute value types */
using AttrValue = std::variant<int64_t, double, std::string, bool>;

/* Attribute change callback type */
using AttrChangeCallback = std::function<void(EntityId, const std::string& key,
                                               const AttrValue& old_val,
                                               const AttrValue& new_val)>;

/* Per-entity attribute table with change notification */
class AttributeTable {
public:
    void Set(const std::string& key, AttrValue value);
    AttrValue Get(const std::string& key, const AttrValue& default_val = {}) const;
    bool Has(const std::string& key) const;
    void Remove(const std::string& key);
    void SetChangeCallback(AttrChangeCallback cb);

private:
    std::unordered_map<std::string, AttrValue> attrs_;
    AttrChangeCallback on_change_;
};
```

### Step 3: Define Entity Lifecycle State Machine

**File**: `src/runtime/entity/entity.h`

```cpp
/* Entity lifecycle states */
enum class EntityState {
    Created,    /* Allocated but not yet active in the world */
    Active,     /* Fully active — receives updates and messages */
    Suspended,  /* Temporarily inactive (e.g., player disconnected, awaiting reconnect) */
    Destroyed   /* Permanently removed — all resources released */
};

/* Base Entity class */
class Entity {
public:
    Entity(EntityId id);
    virtual ~Entity();

    /* Lifecycle */
    EntityId GetId() const { return id_; }
    EntityState GetState() const { return state_; }
    void Activate();
    void Suspend();
    void Destroy();  /* Cancels all timers, removes all listeners, releases ID */

    /* Attributes */
    AttributeTable& Attrs() { return attrs_; }
    const AttributeTable& Attrs() const { return attrs_; }

    /* Components */
    template<typename T>
    T* AddComponent(std::unique_ptr<T> component);
    template<typename T>
    T* GetComponent();
    template<typename T>
    void RemoveComponent();

    /* Connection binding */
    void BindConnection(evpp::TCPConnPtr conn);
    void UnbindConnection();
    evpp::TCPConnPtr GetConnection() const;

    /* Timer management — timers are auto-cancelled on Destroy */
    TimerId AddTimer(int64_t interval_ms, bool repeat, std::function<void()> callback);
    void CancelTimer(TimerId id);

private:
    EntityId id_;
    EntityState state_ = EntityState::Created;
    AttributeTable attrs_;
    std::vector<std::unique_ptr<Entity>> children_;  /* Composition support */
    evpp::TCPConnPtr connection_;
    std::vector<TimerId> owned_timers_;
    std::unordered_map<std::type_index, std::unique_ptr<void, void(*)(void*)>> components_;
};
```

### Step 4: Define Entity Manager

**File**: `src/runtime/entity/entity_manager.h`

```cpp
/* Central registry of all entities */
class EntityManager {
public:
    static EntityManager& Instance();

    Entity* CreateEntity(EntityId id = 0);  /* id=0 means auto-allocate */
    Entity* GetEntity(EntityId id);
    void DestroyEntity(EntityId id);
    void DestroyAll();

    /* Connection lookup */
    Entity* FindByConnection(const evpp::TCPConnPtr& conn);

    /* Iteration */
    void ForEachActive(std::function<void(Entity&)> callback);
    size_t Count() const;

private:
    std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
    std::unordered_map<evpp::TCPConnPtr, EntityId> conn_to_entity_;
    std::unique_ptr<EntityIdAllocator> id_allocator_;
};
```

### Step 5: Lua Binding

**File**: `src/runtime/script/entity_bind.cc`

Expose Entity and EntityManager to Lua:
- `entity.create(id)` — create entity
- `entity:destroy()` — destroy entity
- `entity:get_attr(key)` / `entity:set_attr(key, value)`
- `entity:on_attr_change(key, callback)` — register change listener
- `entity:bind_connection(conn)` / `entity:unbind_connection()`
- `entity:send(data)` — send to bound connection
- `entity:add_timer(interval_ms, repeat, callback)` — auto-cleaned on destroy
- `entity:add_component(name, component_table)` — Lua-level component

### Step 6: Integration with Existing Callbacks

Modify `net_tcp_server_bind.cc` to optionally create entities on connect:

```cpp
/* New server option: auto-create entity on connect */
server:set_auto_entity(true)
```

When enabled, `on_connect` receives an Entity instead of a raw ConnCtx.

### Step 7: Tests

**File**: `src/tests/unit/entity_test.cc`

- Entity lifecycle: Create → Activate → Suspend → Destroy
- Attribute change notification
- Timer auto-cancellation on Destroy
- Connection bind/unbind cycle
- Entity ID allocation uniqueness
- Component add/get/remove

**File**: `src/tests/lua/entity_test.lua`

- Lua-level entity creation and attribute manipulation
- Entity-connection binding in integration with TCP server
- Entity destruction cascades to timer cancellation

## Acceptance Criteria

1. Entity can be created, activated, suspended, and destroyed
2. Entity destruction automatically cancels all owned timers
3. Entity destruction automatically removes all event listeners
4. Attribute changes trigger registered callbacks
5. Connection binding/unbinding works correctly
6. `FindByConnection()` returns the correct entity
7. EntityManager correctly tracks all entities
8. Lua binding exposes all C++ Entity functionality
9. 64-bit entity IDs are unique within a process
10. All unit tests pass

## Dependencies

- None (this is foundational — other plans build on it)

## Estimated Effort

- C++ headers: ~400 lines
- C++ implementation: ~600 lines
- Lua binding: ~300 lines
- Tests: ~200 lines C++ + ~200 lines Lua
- **Total**: ~1500 lines

## Risks

- Entity ID allocation strategy choice impacts distributed deployment; start with sequential for single-process, add snowflake later
- Component model scope creep: start with minimal Component support, expand as needed
- Interaction with existing ConnCtx lifecycle: must ensure ConnCtx disposal and Entity destruction are properly ordered
