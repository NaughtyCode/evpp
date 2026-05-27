# P2-1: AOI System — Spatial Index / Area of Interest

## Objective

Implement a spatial indexing system (grid-based or quadtree) supporting Area of Interest (AOI) queries for entity visibility management.

## Current State

No spatial partitioning, grid, quadtree, or scene graph exists. `PhysicsEngineBridge` integrates Jolt Physics (which has broad-phase spatial structures), but the physics output is not connected to game objects (see P1-5).

## Implementation Steps

### Step 1: Define AOI Spatial Index

**File**: `src/runtime/aoi/spatial_index.h`

```cpp
/* Grid-based spatial index for AOI queries. */
class SpatialGrid {
public:
    SpatialGrid(float world_width, float world_height, float cell_size);

    /* Add/update/remove an entity's position */
    void Insert(EntityId id, float x, float y);
    void Update(EntityId id, float x, float y);
    void Remove(EntityId id);

    /* Query entities within radius of a point */
    std::vector<EntityId> QueryRadius(float x, float y, float radius) const;

    /* Get entities in the same cell + neighboring cells */
    std::vector<EntityId> QueryAOI(EntityId id) const;

private:
    float cell_size_;
    int cols_, rows_;
    std::vector<std::vector<EntityId>> grid_;
    std::unordered_map<EntityId, int> entity_cell_;
};
```

### Step 2: Implement AOI Manager

**File**: `src/runtime/aoi/aoi_manager.h`

```cpp
class AOIManager {
public:
    /* Register entity with its interest radius */
    void RegisterEntity(EntityId id, float aoi_radius);

    /* Called when entity position changes */
    void OnEntityMove(EntityId id, float x, float y);

    /* Get entities visible to this entity */
    std::vector<EntityId> GetVisibleEntities(EntityId id);

    /* Subscribe to enter/leave events */
    using AOIEventCallback = std::function<void(EntityId observer, EntityId target, bool entered)>;
    void SetEventCallback(AOIEventCallback callback);
};
```

### Step 3: Integrate with Entity System

- `Entity::SetPosition()` → updates AOIManager
- `Entity::GetVisibleEntities()` → delegates to AOIManager
- Enter/leave callbacks dispatched to Lua

### Step 4: Lua Binding

```lua
entity:set_position(x, y, z)
local visible = entity:get_visible_entities()  -- returns {entity_id, ...}
entity:on_aoi_enter(function(entity, entered_entity) ... end)
entity:on_aoi_leave(function(entity, left_entity) ... end)
```

### Step 5: Tests

- Insert 10,000 entities, query radius, verify correct results
- Move entity across cell boundary, verify cell update
- AOI enter/leave events on boundary crossing

## Acceptance Criteria

1. SpatialGrid provides O(1) insert/update/remove and O(cell_contents) queries
2. AOIManager tracks entity positions and fires enter/leave events
3. Entities can query visible entities within their AOI radius
4. Performance: 10,000 entities, query < 1ms
5. Tests verify correctness and performance

## Dependencies

- P0-1 (Entity Model)

## Estimated Effort: ~1000 lines
