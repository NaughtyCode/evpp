# AOI (Area of Interest) System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 `aoi.*` 函数均为同步调用，直接操作全局 `AOIManager` 单例（通过 `std::unique_ptr<AOIManager>` 持有）。 |
| **线程安全** | 否。`g_aoi_manager` 是全局变量，无锁保护。AOI 管理器内部使用 `SpatialGrid` 进行空间索引，非线程安全。所有 AOI 操作必须在同一线程上串行调用。 |
| **回调线程** | 仅事件日志。`aoi.init()` 注册了一个内置的 enter/leave 事件回调，该回调在 `RegisterEntity` / `OnEntityMove` / `UnregisterEntity` 调用期间同步触发，仅在调用者线程上打印 DEBUG 日志。没有 Lua 回调机制。 |

## Overview

The AOI (Area of Interest) system provides spatial entity management using a grid-based spatial index. Entities are registered with a position and visibility radius; the system tracks which entities can "see" each other and supports radius queries.

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

初始化时自动注册一个内部 enter/leave 事件回调，打印 DEBUG 级别日志。

### `aoi.register_entity(entity_id, x, y [, aoi_radius])`

Registers an entity in the AOI system and sets its initial position.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Unique entity identifier |
| `x` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | X position in world-space |
| `y` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Y position in world-space |
| `aoi_radius` | `number` | `float` (via `luaL_optnumber`, default 100.0) | Visibility radius (entities within this distance are "visible") |

| Returns | — | 无返回值 |

如果 AOI 未初始化，返回 `nil, "AOI not initialized"`。

### `aoi.update_entity(entity_id, x, y)`

Updates an entity's position. Triggers visibility recomputation — entities entering/leaving the entity's AOI radius fire the enter/leave event callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Entity identifier |
| `x` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | New X position |
| `y` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | New Y position |

| Returns | — | 无返回值（AOI 未初始化时无操作，不报错） |

### `aoi.unregister_entity(entity_id)`

Removes an entity from the AOI system. Fires leave events for all entities that could previously see this entity.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Entity identifier to remove |

| Returns | — | 无返回值（AOI 未初始化时无操作） |

### `aoi.get_visible(entity_id)`

Returns the list of entities visible to the given entity (i.e., within its AOI radius).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Observer entity identifier |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `entities` | `table` | Lua table (array, via `lua_newtable` + `lua_rawseti`) | Array of entity IDs visible to the observer。AOI 未初始化时返回空 table。 |

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

### `aoi.count()`

Returns the total number of registered entities.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `count` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Number of registered entities。AOI 未初始化时返回 0。 |

### `aoi.shutdown()`

Shuts down the AOI system, destroying the spatial grid and clearing all registered entities.

| Returns | — | 无返回值 |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `init` | world_width, world_height, cell_size | `number` | `float` | `luaL_checknumber` / `luaL_optnumber` + `static_cast<float>` |
| 所有实体操作 | entity_id | `integer` | `entity::EntityId` (uint64) | `luaL_checkinteger` + `static_cast<EntityId>` |
| 位置相关 | x, y, new_x, new_y | `number` | `float` | `luaL_checknumber` + `static_cast<float>` |
| `register_entity` | aoi_radius | `number` | `float` (default 100.0f) | `luaL_optnumber` + `static_cast<float>` |
| `query_radius` | radius | `number` | `float` | `luaL_checknumber` + `static_cast<float>` |
| `get_visible` / `query_radius` | 返回值 | `table` (array of integer) | `vector<EntityId>` → `lua_Integer` | `lua_pushinteger` + `lua_rawseti` |
| `count` | 返回值 | `integer` | `size_t` → `lua_Integer` | `lua_pushinteger` |

## Example

```lua
-- Initialize AOI for a 10000x10000 world with 50-unit cells
aoi.init(10000, 10000, 50)

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
-- → Entity 1001 sees entity 1002 (within both radii)

-- Update position (entity 1001 moves closer to 1003)
aoi.update_entity(1001, 4900, 5000)

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

- AOI uses a uniform grid (`SpatialGrid`) — optimal for evenly-distributed entities
- The enter/leave event callback is hardcoded to DEBUG log (not configurable from Lua)
- `aoi_radius` determines an entity's visibility range in both directions (observer and observed)
- Uninitialized AOI gracefully returns empty tables or 0 rather than throwing errors
