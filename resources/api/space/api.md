# Space System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 `space.*` 函数均为同步调用，直接操作 `SpaceManager` 和 `SpaceMessageRouter` 单例。 |
| **线程安全** | 否。`SpaceManager::Instance()` 和 `SpaceMessageRouter::Instance()` 是进程级全局单例，无锁保护。Space 内部持有的 Entity 不跨线程共享。`SpaceMessageRouter` 使用内部队列（消息通过 `_pending_messages` 存储在 Lua 全局 table `space` 中）。所有 Space 操作必须在同一线程上串行调用。**注意：`_pending_messages` 存储在 Lua 全局 `space` table 中，每个 VM 有独立的 Lua state，因此每个 VM 有独立的消息队列。** |
| **回调线程** | 无回调。采用主动轮询模式——每帧调用 `space.poll()` 从 `space._pending_messages` 队列获取跨空间消息。消息通过 `space._deliver_message` 推入队列，不由回调触发。 |

## Overview

The Space system provides multi-space (room/zone/instance) management for game worlds. Each Space is an isolated container with its own entities, scripts, and configuration. Cross-space communication uses a message-passing pattern with poll-based delivery.

## Module

`space` (global table)

## Functions

### `space.create(name, config)`

Creates a new Space (game room / zone / instance).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `name` | `string` | `const char*` (via `luaL_checkstring`) | Space name |
| `config` | `table` | Lua table (via `lua_getfield`) | Space configuration (optional fields below) |

**`config` 字段：**

| Field | Type | C Type | Required | Description |
|-------|------|--------|----------|-------------|
| `max_entities` | `integer` | `lua_Integer` → `size_t` (via `lua_tointeger`) | No | Maximum entities in this space |
| `max_players` | `integer` | `lua_Integer` → `size_t` (via `lua_tointeger`) | No | Maximum players in this space |
| `scripts` | `table` (array of strings) | `std::vector<std::string>` (via `lua_next` + `lua_tostring`) | No | Entry script filenames to load into this space |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `space_id` | `integer` | `lua_Integer` (via `lua_pushinteger`) | New space ID on success |
| `nil + err` | `nil` + `string` | `lua_pushnil` + `lua_pushstring` | `nil` + error message on failure |

返回值数量：成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

若提供了 `scripts`，`CreateSpace` 后立即调用 `LoadScripts()` 加载入口脚本。

### `space.get(space_id)`

Retrieves information about a specific space.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `space_id` | `integer` | `lua_Integer` → `space::SpaceId` (via `luaL_checkinteger`) | Space identifier |

| Returns | Type | Description |
|---------|------|-------------|
| `info` | `table` or `nil` | Table with fields: `id` (integer), `name` (string), `entity_count` (integer). `nil` if space not found. |

### `space.destroy(space_id)`

Destroys a space and all its entities.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `space_id` | `integer` | `lua_Integer` → `space::SpaceId` (via `luaL_checkinteger`) | Space identifier to destroy |

| Returns | — | 无返回值 |

### `space.send(space_id, target_entity, payload)`

Sends a cross-space message to an entity in another space.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `space_id` | `integer` | `lua_Integer` → `space::SpaceId` (via `luaL_checkinteger`) | Target space ID |
| `target_entity` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Target entity ID within the target space |
| `payload` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Message payload (binary data) |

| Returns | — | 无返回值 |

消息通过 `SpaceMessageRouter` 路由到目标空间，最终由目标空间的 `_deliver_message` 内部函数推入 `_pending_messages` 队列。

### `space.list()`

Lists all active spaces.

| Returns | Type | Description |
|---------|------|-------------|
| `spaces` | `table` | Array of space info tables, each with fields: `id` (integer), `name` (string), `entity_count` (integer) |

### `space.current()`

Returns information about the default space.

| Returns | Type | Description |
|---------|------|-------------|
| `info` | `table` or `nil` | Space info table with fields: `id`, `name`, `entity_count`. `nil` if no default space exists. |

### `space.poll()`

Polls for the next pending cross-space message (non-blocking). Each call returns at most **one** message. Call in a loop until `nil` to drain all pending messages.

| Returns | Type | Description |
|---------|------|-------------|
| `msg` | `table` or `nil` | Message table with fields: `source_space` (integer), `source_entity` (integer), `target_entity` (integer), `payload` (string). `nil` if no messages pending. |

消息按 FIFO 顺序从 `space._pending_messages` 队列中取出。应在每帧调用以处理跨空间通信。

## 消息格式

**`space.poll()` 返回的消息 table：**

| Field | Type | Description |
|-------|------|-------------|
| `source_space` | `integer` | 发送方所在空间 ID |
| `source_entity` | `integer` | 发送方实体 ID |
| `target_entity` | `integer` | 接收方实体 ID |
| `payload` | `string` | 消息负载（二进制数据） |

## Internal Functions

以下函数是模块内部使用的，已在 Lua 中注册但不建议直接调用：

| Function | Description |
|----------|-------------|
| `space._deliver_message(src_space, src_entity, tgt_entity, payload)` | 由 `SpaceMessageRouter` 调用，将消息推入 `_pending_messages` 队列 |
| `space._on_connection_data(conn_lightuserdata, data)` | 默认空实现，游戏脚本可在 Lua 中覆盖 |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `create` | name | `string` | `const char*` | `luaL_checkstring` |
| `create` | config table | `table` | Lua table | `lua_getfield` for each field |
| `create` | config.max_entities / max_players | `integer` | `size_t` | `lua_tointeger` |
| `create` | config.scripts | `table` (array) | `std::vector<std::string>` | `lua_next` + `lua_tostring` |
| `create` → | space_id | `integer` | `space::SpaceId` → `lua_Integer` | `lua_pushinteger` |
| `get` / `destroy` / `send` | space_id | `integer` | `space::SpaceId` | `luaL_checkinteger` + static_cast |
| `send` | target_entity | `integer` | `entity::EntityId` | `luaL_checkinteger` + static_cast |
| `send` | payload | `string` | `const char*` + `size_t` | `luaL_checklstring` |
| `get` / `list` / `current` → | info table | `table` | Lua table (new) | `lua_newtable` + `lua_setfield` |
| `poll` → | msg table | `table` or `nil` | Lua table (from queue) | `lua_rawgeti` + `lua_rawseti` for shift |

## Example

```lua
-- Create spaces
local lobby_id = space.create("Lobby", {
    max_entities = 1000,
    max_players = 500,
})

local dungeon_id = space.create("Dungeon_001", {
    max_entities = 200,
    max_players = 5,
    scripts = {"dungeon_init.lua"},
})

-- List all spaces
local spaces = space.list()
for _, sp in ipairs(spaces) do
    log_info(string.format("Space #%d: %s (%d entities)",
        sp.id, sp.name, sp.entity_count))
end

-- Get current space info
local current = space.current()
if current then
    log_info("Current space: " .. current.name)
end

-- Send cross-space message
-- Entity in lobby tells entity in dungeon something
space.send(dungeon_id, target_entity_id, cmsgpack.pack({
    type = "invite",
    from = "player1",
}))

-- Poll for cross-space messages (call each frame)
local msg = space.poll()
while msg do
    log_info(string.format("Space msg: space %d → entity %d: %s",
        msg.source_space, msg.target_entity, msg.payload))
    -- Dispatch to target entity
    msg = space.poll()
end

-- Cleanup
space.destroy(dungeon_id)
space.destroy(lobby_id)
```

## Notes

- Spaces are isolated containers — entities in different spaces cannot directly see each other
- Cross-space communication is asynchronous via message passing with poll-based delivery
- `space._pending_messages` 是一个 Lua 全局 table 数组，存储在 `space` 模块 table 中
- `space._on_connection_data` 可在 Lua 中覆盖以实现自定义连接数据处理
- Space scripts (entry scripts) are loaded with the space's own `ScriptVM` if one is configured
