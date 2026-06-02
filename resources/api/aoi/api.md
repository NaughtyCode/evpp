# AOI (Area of Interest) System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 `aoi.*` 函数均为同步调用，操作当前 ScriptVM registry 中保存的 `AOIManager` 实例。 |
| **线程安全** | 否。每个 `lua_State` 拥有独立 AOI 状态，但 `AOIManager` / `SpatialGrid` 本身不提供跨线程同步。所有 AOI 操作必须在该 ScriptVM 所属线程串行调用。 |
| **回调线程** | 调用者线程。`set_event_callback()` 注册的 Lua 回调会在 `register_entity` / `update_entity` / `unregister_entity` 触发可见性变化时同步调用。回调执行期间禁止再次修改 AOI。 |

## Overview

The AOI (Area of Interest) system provides spatial entity management using a grid-based spatial index. Entities are registered with a position and an observer-side visibility radius. The system tracks the directional set of targets visible to each observer and also supports raw radius queries.

Current semantics:

- Visibility is directional: `A` can see `B` when `B` is inside `A`'s `aoi_radius`; this does not imply `B` can see `A`.
- If two entities use the same radius and are both within that distance, visibility will usually appear symmetric, but symmetry is not a separate contract.
- `get_visible(entity_id)` returns the observer's maintained visible set, excludes the observer itself, and is sorted by entity id.
- `query_radius(x, y, radius)` is a raw spatial query. It includes every entity in range, including a caller's own entity if that entity is in range, and it does not provide a stable ordering contract.
- Coordinates outside the world bounds are assigned to the nearest boundary cell for indexing. The stored original position is still used by precise distance checks.
- The module has no `space_id`, `layer_id`, `phase_id`, team, stealth, owner-only, occlusion, batching, or replication-budget filtering.

## Module

`aoi` (global table)

## Functions

### `aoi.init(world_width, world_height [, cell_size])`

Initializes the AOI system with a spatial grid.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `world_width` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | World width in world-space units |
| `world_height` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | World height in world-space units |
| `cell_size` | `number` | `float` (via `luaL_optnumber`, default 50.0) | Grid cell size. Smaller cells = more memory, better query performance for dense areas. |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |
| `nil, err` | `nil, string` | Lua stack values | Returned when AOI initialization fails after argument validation, for example an oversized grid allocation |

无效参数会通过 `luaL_argerror` 抛出 Lua 参数错误。再次调用会先构造新的空间索引和 AOI manager；构造成功后才清理并替换当前 AOI 实例、事件回调和已注册实体。构造失败返回 `nil, err`，旧 AOI 状态保持不变。

### `aoi.set_event_callback(callback_or_nil)`

Registers or clears the Lua enter/leave callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback_or_nil` | `function` or `nil` | Lua registry ref | Callback signature: `function(observer_id, target_id, entered)`。传 `nil` 清空回调。 |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `boolean` | `true` on success |
| `nil, err` | `nil, string` | Returned when AOI is not initialized or when called from an AOI callback |

`entered == true` 表示 `target_id` 进入 `observer_id` 的可见集合，`false` 表示离开。回调异常会记录日志并恢复 Lua 栈，不会向外继续抛出。

### `aoi.register_entity(entity_id, x, y [, aoi_radius])`

Registers an entity in the AOI system and sets its position.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Unique entity identifier |
| `x` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | X position in world-space |
| `y` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Y position in world-space |
| `aoi_radius` | `number` | `float` (via `luaL_optnumber`, default 100.0) | Observer-side visibility radius. Targets within this distance become visible to this entity. |

| Returns | Type | Description |
|---------|------|-------------|
| success | — | 成功无返回值 |
| failure | `nil, string` | AOI 未初始化、回调内修改或底层异常时返回 `nil, err` |

如果 AOI 未初始化，返回 `nil, "AOI not initialized"`。如果同一 `entity_id` 已存在，本接口是 upsert：半径和位置会作为一次 AOI mutation 原子更新，不会先在旧位置产生临时 enter/leave。不要把 re-register 当作普通移动接口使用；普通移动应调用 `aoi.update_entity`，只改半径应调用 `aoi.update_radius`。

### `aoi.update_entity(entity_id, x, y)`

Updates an entity's position. It recomputes visibility for the mover and for nearby observers that might gain or lose sight of the mover.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Entity identifier |
| `x` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | New X position |
| `y` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | New Y position |

| Returns | Type | Description |
|---------|------|-------------|
| success | — | 成功无返回值 |
| failure | `nil, string` | AOI 未初始化、回调内修改或底层异常时返回 `nil, err` |

事件语义是方向性的：移动者进入静止 observer 的半径时，会触发 `observer -> mover` 的 enter；移动者自己的可见集合变化时，也会触发 `mover -> target` 的 enter/leave。

### `aoi.update_radius(entity_id, aoi_radius)`

Updates only an entity's observer-side visibility radius.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Entity identifier |
| `aoi_radius` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | New observer-side visibility radius |

| Returns | Type | Description |
|---------|------|-------------|
| success | — | 成功无返回值 |
| failure | `nil, string` | AOI 未初始化、回调内修改或底层异常时返回 `nil, err` |

This recomputes only this observer's maintained visible set. Other observers are unaffected because the current implementation has no target-side aura.

### `aoi.unregister_entity(entity_id)`

Removes an entity from the AOI system.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Entity identifier to remove |

| Returns | Type | Description |
|---------|------|-------------|
| success | — | 成功无返回值 |
| failure | `nil, string` | AOI 未初始化或回调内修改时返回 `nil, err` |

注销会发送两类 leave 事件：被删除实体自己当前可见的 targets 会收到 `deleted_entity -> target` leave；曾经能看到该实体的其他 observers 会收到 `observer -> deleted_entity` leave。

### `aoi.get_visible(entity_id)`

Returns the sorted list of entities visible to the given observer.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Observer entity identifier |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `entities` | `table` | Lua table (array, via `lua_newtable` + `lua_rawseti`) | Array of entity IDs visible to the observer, sorted by entity id and excluding the observer itself。AOI 未初始化时返回空 table。 |

### `aoi.query_radius(x, y, radius)`

Queries all entities within a circular radius of a point.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `x` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Query center X |
| `y` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Query center Y |
| `radius` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Search radius |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `entities` | `table` | Lua table (array, via `lua_newtable` + `lua_rawseti`) | Array of entity IDs within the search radius。AOI 未初始化时返回空 table。 |

This is a raw spatial query. It does not exclude any caller entity and does not sort the result. If a network protocol needs deterministic order, sort explicitly at the caller.

### `aoi.count()`

Returns the total number of registered entities.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `count` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Number of registered entities。AOI 未初始化时返回 0。 |

### `aoi.shutdown()`

Shuts down the AOI system, destroying the spatial grid and clearing all registered entities.

| Returns | Type | Description |
|---------|------|-------------|
| success | — | 成功无返回值 |
| failure | `nil, string` | 回调内修改时返回 `nil, err` |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `init` | world_width, world_height, cell_size | `number` | `float` | `luaL_checknumber` / `luaL_optnumber` + `static_cast<float>` |
| `set_event_callback` | callback_or_nil | `function` or `nil` | registry ref | `luaL_ref` / `luaL_unref` |
| 所有实体操作 | entity_id | `integer` | `entity::EntityId` (uint64) | `luaL_checkinteger` + `static_cast<EntityId>` |
| 位置相关 | x, y, new_x, new_y | `number` | `float` | `luaL_checknumber` + `static_cast<float>` |
| `register_entity` | aoi_radius | `number` | `float` (default 100.0f) | `luaL_optnumber` + `static_cast<float>` |
| `update_radius` | aoi_radius | `number` | `float` | `luaL_checknumber` + `static_cast<float>` |
| `query_radius` | radius | `number` | `float` | `luaL_checknumber` + `static_cast<float>` |
| `get_visible` / `query_radius` | 返回值 | `table` (array of integer) | `vector<EntityId>` → `lua_Integer` | `lua_pushinteger` + `lua_rawseti` |
| `count` | 返回值 | `integer` | `size_t` → `lua_Integer` | `lua_pushinteger` |

## Example

```lua
-- Initialize AOI for a 10000x10000 world with 50-unit cells
aoi.init(10000, 10000, 50)
aoi.set_event_callback(function(observer, target, entered)
    local event = entered and "entered" or "left"
    log_debug(string.format("AOI: %d %s %d", target, event, observer))
end)

-- Register entities
aoi.register_entity(1001, 100, 200, 150)  -- entity 1001 at (100,200), sees 150 units
aoi.register_entity(1002, 150, 250, 120)  -- entity 1002 at (150,250), sees 120 units
aoi.register_entity(1003, 5000, 5000, 200) -- entity 1003 far away

-- Check who entity 1001 can see
local visible = aoi.get_visible(1001)
log_info("Entity 1001 sees " .. #visible .. " entities")
for _, eid in ipairs(visible) do
    log_info("  - sees entity " .. eid)
end
-- → Entity 1001 sees entity 1002 because 1002 is within 1001's radius

-- Update position (entity 1001 moves closer to 1003)
aoi.update_entity(1001, 4900, 5000)

-- Update observer-side AOI radius without moving the entity
aoi.update_radius(1001, 250)

-- Radius query (find all entities near a point)
local nearby = aoi.query_radius(5000, 5000, 300)
log_info("Entities near (5000,5000): " .. #nearby)

-- Stats
log_info("Total registered: " .. aoi.count())

-- Cleanup
aoi.unregister_entity(1001)
aoi.shutdown()
```

## Notes

- AOI uses a uniform grid (`SpatialGrid`) and is best suited to bounded, reasonably even 2D/2.5D maps.
- The enter/leave event callback is configured by `aoi.set_event_callback(callback_or_nil)` and is dispatched synchronously on the caller thread.
- AOI mutation APIs return `nil, err` instead of raising for not-initialized state and callback reentrancy guard failures. Invalid Lua argument types/ranges still raise Lua argument errors via `luaL_argerror`.
- `aoi_radius` is observer-side only. There is no target-side aura or separate replication radius in the current implementation.
- `get_visible` is sorted and excludes self; `query_radius` is unsorted and includes all entities in range.
- Uninitialized AOI gracefully returns empty tables or 0 for query/count functions rather than throwing errors.
