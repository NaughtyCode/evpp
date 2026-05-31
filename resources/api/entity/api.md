# Entity System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 entity 实例方法必须在 ScriptVM 所属线程上调用。 |
| **线程安全** | 否。`EntityCtx` 通过 light userdata 存储在 Lua 实例表中，per-VM。但 `EntityManager::Instance()` 是进程级全局单例，非线程安全——多个 ScriptVM 不可并发操作 Entity。Entity 的 Lua component ref 存储在各自 `lua_State` registry 中，不可跨 VM 共享。 |
| **回调线程** | 调用者线程。Entity 定时器回调通过 `EntityManager` 的定时器机制在调用者线程上触发。`entity:send()` 委托给绑定的 conn 实例的 `send` 方法。 |

## Overview

The Entity system provides Lua bindings for the ECS (Entity-Component-System) architecture. Entities are created via `entity.create()`, returning a Lua instance table with methods for attribute management, component attachment, network binding, and per-entity timers.

## Module

`entity` (global table, callable for `entity.create()`)

## Static Functions

### `entity.create([id])`

Creates a new entity. If no ID is specified, the `EntityManager` assigns one automatically.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `id` | `integer` | `lua_Integer` → `entity::EntityId` (via `luaL_checkinteger`) | Optional entity ID。若省略或为 nil，由 EntityManager 自动分配。 |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable` + metatable) | Entity instance table。内部 `_ctx` 存储 light userdata (`EntityCtx*`)。创建后自动调用 `Activate()`。 |

重复 ID 会抛出 Lua error。

---

## Entity Instance Methods

### `entity:destroy()`

Destroys the entity. Releases all timers, connection bindings, and Lua components.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already disposed |

销毁流程：
1. 设置 `disposed = true`
2. 释放所有 timer callback ref
3. 释放 conn ref
4. 调用 `EntityManager::DestroyEntity()`
5. 清除 `_ctx` 字段、delete `EntityCtx`

### `entity:get_id()`

Returns the entity's unique identifier.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `id` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Entity ID |

已销毁的 entity 会抛出 Lua error。

### `entity:get_state()`

Returns the entity's current lifecycle state.

| Returns | Type | Description |
|---------|------|-------------|
| `state` | `string` | One of: `"created"`, `"active"`, `"suspended"`, `"destroyed"`, `"unknown"` |

已销毁的 entity 或 entity 不存在时抛出 Lua error。

### `entity:activate()`

Activates the entity (transitions from `Created` or `Suspended` to `Active`).

| Returns | — | 无返回值 |

### `entity:suspend()`

Suspends the entity (transitions from `Active` to `Suspended`).

| Returns | — | 无返回值 |

### `entity:get_attr(key)`

Gets an attribute value by key name.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `key` | `string` | `const char*` (via `luaL_checkstring`) | Attribute name |

| Returns | Type | Description |
|---------|------|-------------|
| `value` | `integer` / `number` / `string` / `boolean` | Value converted from `AttrValue` variant via `std::visit`. Type depends on what was stored: `int64_t` → integer, `double` → number, `std::string` → string, `bool` → boolean. |

### `entity:set_attr(key, value)`

Sets an attribute value by key name. The value type is auto-detected from the Lua type.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `key` | `string` | `const char*` (via `luaL_checkstring`) | Attribute name |
| `value` | `integer` / `number` / `string` / `boolean` / `nil` | Variant type (see below) | Value to store. `nil` removes the attribute. |

**类型映射：**

| Lua Value Type | C++ AttrValue |
|----------------|---------------|
| `integer` (`lua_isinteger`) | `int64_t` |
| `number` (float) | `double` |
| `string` | `std::string` |
| `boolean` | `bool` |
| `nil` | remove existing attribute |
| 其他类型 | 抛出 Lua error |

| Returns | — | 无返回值 |

### `entity:remove_attr(key)`

Removes an attribute by key name.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `key` | `string` | `const char*` (via `luaL_checkstring`) | Attribute name |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `removed` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the attribute existed and was removed |

### `entity:has_attr(key)`

Checks if an attribute exists.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `key` | `string` | `const char*` (via `luaL_checkstring`) | Attribute name |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `exists` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the attribute exists |

### `entity:attr_count()`

Returns the current number of stored attributes.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `count` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Attribute count |

### `entity:list_attrs()`

Returns all attribute keys as a 1-based Lua array.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `keys` | `table` | Lua table (array, via `lua_createtable` + `lua_rawseti`) | Attribute key strings |

### `entity:bind_connection(conn)`

Binds a network connection instance (e.g., a `net.server` conn table) to this entity. The entity's `send()` method delegates to `conn:send()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `conn` | `table` or `nil` | Lua table ref (via `luaL_ref`) | Connection instance table with a `send` method. Pass `nil` to unbind. |

| Returns | — | 无返回值 |

重新调用会释放旧 conn ref 并替换。

### `entity:get_connection()`

Returns the bound network connection instance, or `nil` if none.

| Returns | Type | Description |
|---------|------|-------------|
| `conn` | `table` or `nil` | The bound connection table, or `nil` |

### `entity:send(data)`

Sends data through the bound network connection. Delegates to `conn:send(data)` via `lua_pcall`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Binary data to send |

| Returns | — | 无返回值。send 错误通过 ENGINE_LOG_ERROR 记录，不抛出 Lua error。 |

必须先调用 `bind_connection()` 绑定连接，否则抛出 Lua error。

### `entity:add_timer(interval_ms, repeat, callback)`

Adds a per-entity timer. The callback fires while the entity is active.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `interval_ms` | `integer` | `int64_t` (via `luaL_checkinteger`) | Interval in milliseconds |
| `repeat` | `boolean` | `bool` (via `lua_toboolean`) | `true` for repeating timer, `false` for one-shot |
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Timer callback. Signature: `function()` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `timer_id` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Timer ID for use with `cancel_timer()` |

**Callback 行为：**
- 仅当 entity 状态为 `Active` 时调用回调
- 若 entity 非 Active，自动释放 callback ref 并跳过
- 一次性定时器触发后自动释放 ref
- 重复定时器的 ref 被追踪在 `EntityCtx::timer_refs` 中，在 cancel/destroy 时释放

### `entity:cancel_timer(timer_id)`

Cancels a per-entity timer and releases its Lua callback ref.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `timer_id` | `integer` | `TimerId` (uint64, via `luaL_checkinteger` + `static_cast`) | Timer ID returned by `add_timer()` |

| Returns | — | 无返回值 |

### `entity:add_component(name, component_table)`

Attaches a Lua table as a named component to the entity. Replaces any existing component with the same name.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `name` | `string` | `const char*` (via `luaL_checkstring`) | Component name |
| `component_table` | `table` | Lua table (via `luaL_ref`) | The component table to attach |

| Returns | — | 无返回值 |

旧同名组件的 registry ref 自动释放。

### `entity:get_component(name)`

Retrieves a previously attached Lua component by name.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `name` | `string` | `const char*` (via `luaL_checkstring`) | Component name |

| Returns | Type | Description |
|---------|------|-------------|
| `component` | `table` or `nil` | The component table, or `nil` if not found |

### `entity:remove_component(name)`

Removes a Lua component by name and releases its registry ref.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `name` | `string` | `const char*` (via `luaL_checkstring`) | Component name |

| Returns | — | 无返回值 |

---

## Entity Lifecycle States

| State | Description |
|-------|-------------|
| `"created"` | Entity exists but is not yet active |
| `"active"` | Entity is active (timers fire, components usable) |
| `"suspended"` | Entity is paused (timers do not fire) |
| `"destroyed"` | Entity is destroyed and cannot be used |

`entity.create()` calls `Activate()` automatically, so the entity starts in the `"active"` state.

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `create` | id | `integer` or `nil` | `entity::EntityId` (uint64) | `luaL_checkinteger` / nil → auto-assign |
| `get_id` | 返回值 | `integer` | `entity::EntityId` → `lua_Integer` | `lua_pushinteger` |
| `get_state` | 返回值 | `string` | `const char*` | `lua_pushstring` |
| `get_attr` / `set_attr` | key | `string` | `const char*` | `luaL_checkstring` |
| `get_attr` | 返回值 | typed | `AttrValue` variant | `std::visit` with type dispatch |
| `set_attr` | value (int) | `integer` | `int64_t` | `lua_isinteger` → `lua_tointeger` |
| `set_attr` | value (float) | `number` | `double` | `!lua_isinteger` → `lua_tonumber` |
| `set_attr` | value (string) | `string` | `std::string` | `lua_tolstring` |
| `set_attr` | value (bool) | `boolean` | `bool` | `lua_toboolean` |
| `set_attr` | value (`nil`) | `nil` | remove attr | `lua_type == LUA_TNIL` |
| `remove_attr` / `has_attr` | 返回值 | `boolean` | `bool` | `lua_pushboolean` |
| `attr_count` | 返回值 | `integer` | `size_t` → `lua_Integer` | `lua_pushinteger` |
| `list_attrs` | 返回值 | `table` | `std::vector<std::string>` | `lua_createtable` + `lua_rawseti` |
| `bind_connection` | conn | `table` or `nil` | `int` (registry ref) | `luaL_ref` / `luaL_unref` |
| `send` | data | `string` | `const char*` + `size_t` | `luaL_checklstring` |
| `add_timer` | interval_ms | `integer` | `int64_t` | `luaL_checkinteger` |
| `add_timer` | repeat | `boolean` | `bool` | `lua_toboolean` |
| `add_timer` | callback | `function` | `int` (registry ref) | `luaL_ref` |
| `cancel_timer` | timer_id | `integer` | `TimerId` (uint64) | `luaL_checkinteger` + static_cast |
| `add_component` | component_table | `table` | `int` (registry ref) | `luaL_ref` |

## Example

```lua
-- Create an entity
local e = entity.create()
log_info("Created entity: " .. e:get_id())

-- Set attributes
e:set_attr("name", "player1")
e:set_attr("hp", 100)
e:set_attr("max_hp", 100)
e:set_attr("alive", true)
e:set_attr("speed", 5.5)

-- Read attributes
local hp = e:get_attr("hp")
log_info("HP: " .. hp)

-- Check attribute existence
if e:has_attr("mana") then
    log_info("Has mana: " .. e:get_attr("mana"))
end
log_info("Attribute count: " .. e:attr_count())
for _, key in ipairs(e:list_attrs()) do
    log_debug("attr key: " .. key)
end
e:remove_attr("speed")

-- Lua components (ECS pattern)
e:add_component("movement", {
    x = 0, y = 0, z = 0,
    update = function(self, dt)
        self.x = self.x + dt * 10
    end
})
local move = e:get_component("movement")
if move then
    move:update(0.016)
end
e:remove_component("movement")

-- Bind a network connection
local conn = ... -- from net.server on_connect callback
e:bind_connection(conn)
e:send(cmsgpack.pack({action = "welcome", id = e:get_id()}))

-- Per-entity timer (repeating)
local tid = e:add_timer(1000, true, function()
    if not e:has_attr("alive") or not e:get_attr("alive") then
        e:cancel_timer(tid)
        return
    end
    log_info("Entity " .. e:get_id() .. " heartbeat")
end)

-- One-shot timer
e:add_timer(5000, false, function()
    e:set_attr("buff_active", false)
    log_info("Buff expired")
end)

-- Cleanup
e:destroy()
```

## Notes

- Entity instances support `__gc` metamethod — unreferenced entities are automatically destroyed
- All instance methods check `ctx->disposed` and throw Lua error for destroyed entities
- Timer callbacks check entity state (`Active`) each invocation; inactive entities skip the callback
- `ShutdownEntityBindings()` destroys all remaining entities during engine shutdown
