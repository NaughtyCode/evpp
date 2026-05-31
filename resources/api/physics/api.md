# Physics System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | Game 线程（发送命令）和 Physics 线程（执行模拟）。命令通过 lock-free SPSC 队列从 Game 线程推送到 Physics 线程，结果通过另一 SPSC 队列从 Physics 线程返回到 Game 线程。 |
| **线程安全** | 是（无锁队列架构）。`PhysicsCommand` 和 `PhysicsFrameResult` 通过 SPSC 队列传递——推送端和弹出端各自独占一个线程，无竞争。但命令和结果结构体本身不可跨线程共享。 |
| **回调线程** | 无回调。结果通过主动轮询获取（Game 线程从结果队列弹出 `PhysicsFrameResult`）。碰撞事件和变换数据作为帧结果的一部分返回，而非通过回调。 |

## Overview

The Physics system uses Jolt Physics for rigid body simulation. It operates on a command-based architecture where the game thread sends commands to the physics thread via a lock-free queue, and the physics thread returns frame results with transforms and collision events.

## Module

C++ API via `PhysicsCommand` structures. The same subsystem is also exposed to Lua through the `physics` module documented below.

## Command Types

| Command | Description |
|---------|-------------|
| `Spawn` | Create a new physics body from a prototype |
| `Destroy` | Remove a physics body |
| `ApplyForce` | Apply a force to a body |
| `SetVelocity` | Set a body's linear velocity |
| `Tick` | Advance the simulation by one step |

## Command Structures (C++)

### SpawnArgs

| Field | Type | Description |
|-------|------|-------------|
| `proto_id` | `string` | Prototype identifier for body creation settings |
| `position` | `RVec3` | World-space position (default: zero) |
| `rotation` | `Quat` | World-space rotation (default: identity) |
| `user_data` | `uint64_t` | Application-defined user data |

### DestroyArgs

| Field | Type | Description |
|-------|------|-------------|
| `body_id` | `uint32_t` | ID of the body to destroy |

### ApplyForceArgs

| Field | Type | Description |
|-------|------|-------------|
| `body_id` | `uint32_t` | Target body ID |
| `force` | `Vec3` | Force vector to apply |
| `point` | `RVec3` | World-space application point |

### SetVelocityArgs

| Field | Type | Description |
|-------|------|-------------|
| `body_id` | `uint32_t` | Target body ID |
| `velocity` | `Vec3` | Target linear velocity |

### TickArgs

| Field | Type | Description |
|-------|------|-------------|
| `frame_id` | `uint64_t` | Frame sequence number |
| `delta_time` | `float` | Fixed timestep for this tick |

## PhysicsCommand

A tagged union over the 5 command types:

```cpp
PhysicsCommand cmd = PhysicsCommand::MakeSpawn({...});
```

Convenience constructors:
- `MakeSpawn(SpawnArgs)`
- `MakeDestroy(DestroyArgs)`
- `MakeApplyForce(ApplyForceArgs)`
- `MakeSetVelocity(SetVelocityArgs)`
- `MakeTick(TickArgs)`

## Result Structures (C++)

### PhysicsFrameResult

| Field | Type | Description |
|-------|------|-------------|
| `frame_id` | `uint64_t` | Echoed frame ID |
| `transforms` | `vector<BodyTransform>` | Updated body transforms |
| `collision_events` | `vector<CollisionEvent>` | Collision events this frame |
| `diff_packets` | `vector<DiffPacket>` | Per-body change diffs |
| `error` | `string` | Error message (empty = ok) |

### BodyTransform

| Field | Type | Description |
|-------|------|-------------|
| `body_id` | `uint32_t` | Body identifier |
| `pos_x/y/z` | `double` | World-space position |
| `rot_x/y/z/w` | `float` | Rotation quaternion |

### CollisionEvent

| Field | Type | Description |
|-------|------|-------------|
| `body_a` | `uint32_t` | First colliding body |
| `body_b` | `uint32_t` | Second colliding body |
| `type` | `enum` | `Start` (contact added), `Persist` (continued), `End` (contact removed) |
| `contact_points` | `vector<RVec3>` | World-space contact points |

### DiffPacket

| Field | Type | Description |
|-------|------|-------------|
| `object_id` | `uint32_t` | Object identifier |
| `change_mask` | `uint16_t` | Bitmask: bit0=position, bit1=rotation, bit2=linearVel, bit3=angularVel |
| `values` | `vector<float>` | Changed values ordered by mask bit index |

## Configuration

Physics configuration is loaded from `resources/config/physics/physics.json` (via `PhysicsConfig`). Key settings include:

- Fixed timestep (delta time per physics tick)
- Gravity vector
- Broad phase type
- Collision layers and material mappings

---

## Lua API (`physics`)

### 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为 Game 线程）。命令（spawn / destroy / apply_force / set_velocity）通过 lock-free SPSC 队列异步推送到 Physics 线程。查询（get_transform / get_velocity / is_active / ray_cast / get_stats）同步读取最近一次物理帧的结果。`save_state()` / `restore_state()` / `recover()` 为同步调用。 |
| **线程安全** | 部分。命令推送通过 SPSC 队列（无锁，Game 线程独占推送端）。查询读取 `PhysicsSystem` 内部缓存的物理状态——该状态在 Physics 线程写入，Game 线程读取，依赖于 atomic 或 frame boundary 同步。多个 Game 线程不可同时调用。 |
| **回调线程** | 无回调。所有函数均为同步调用。命令立即返回（不等待物理线程执行结果），查询立即返回（读取上次物理帧结果）。结果通过 `PhysicsFrameResult` 返回给调用者。 |

### Overview

The `physics` Lua module provides bindings for rigid body simulation via Jolt Physics. Commands are enqueued asynchronously to the physics thread; queries read the most recent frame's results synchronously. Additionally, `log_*` functions are registered as globals for physics-specific logging (prefixed with `[physics_lua]`).

### Module

`physics`

### Functions

#### `physics.spawn(proto_id, x, y, z, qx, qy, qz, qw [, user_data])`

Enqueues a spawn command for a physics body.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `proto_id` | `string` | `const char*` (via `luaL_checkstring`) | Physics body prototype identifier |
| `x` | `number` | `double` (via `luaL_checknumber`) | World-space position X |
| `y` | `number` | `double` (via `luaL_checknumber`) | World-space position Y |
| `z` | `number` | `double` (via `luaL_checknumber`) | World-space position Z |
| `qx` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Rotation quaternion X |
| `qy` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Rotation quaternion Y |
| `qz` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Rotation quaternion Z |
| `qw` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Rotation quaternion W |
| `user_data` | `integer` | `uint64_t` (via `luaL_checkinteger` + `static_cast`), default 0 | Application-defined user data |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `body_id` | `integer` | `lua_Integer` (via `lua_pushinteger`) | Placeholder body_id (0)。实际的 body_id 在下一次物理帧结果中通过 transforms 返回。 |

失败（物理未初始化）返回 `nil, "physics not initialized"`。

#### `physics.destroy(body_id)`

Enqueues a destroy command for a physics body.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Body identifier to destroy |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if command enqueued |

失败（物理未初始化）返回 `nil, "physics not initialized"`。

#### `physics.apply_force(body_id, fx, fy, fz, px, py, pz)`

Applies a force to a physics body at a world-space point.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Target body ID |
| `fx` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Force X component |
| `fy` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Force Y component |
| `fz` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Force Z component |
| `px` | `number` | `double` (via `luaL_checknumber`) | Application point X |
| `py` | `number` | `double` (via `luaL_checknumber`) | Application point Y |
| `pz` | `number` | `double` (via `luaL_checknumber`) | Application point Z |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if command enqueued |

#### `physics.set_velocity(body_id, vx, vy, vz)`

Sets the linear velocity of a physics body.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Target body ID |
| `vx` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Velocity X |
| `vy` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Velocity Y |
| `vz` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Velocity Z |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if command enqueued |

#### `physics.get_transform(body_id)`

Gets the current transform of a physics body from the most recent frame result.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Body identifier |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `x` | `number` | `lua_pushnumber` | Position X |
| `y` | `number` | `lua_pushnumber` | Position Y |
| `z` | `number` | `lua_pushnumber` | Position Z |
| `qx` | `number` | `lua_pushnumber` | Rotation quaternion X |
| `qy` | `number` | `lua_pushnumber` | Rotation quaternion Y |
| `qz` | `number` | `lua_pushnumber` | Rotation quaternion Z |
| `qw` | `number` | `lua_pushnumber` | Rotation quaternion W |

返回值数量：成功返回 7 个值，失败返回 2 个值 (`nil, "body not found"`)。

#### `physics.get_velocity(body_id)`

Gets the current linear velocity of a physics body.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Body identifier |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `vx` | `number` | `lua_pushnumber` | Velocity X |
| `vy` | `number` | `lua_pushnumber` | Velocity Y |
| `vz` | `number` | `lua_pushnumber` | Velocity Z |

返回值数量：成功返回 3 个值，失败返回 2 个值 (`nil, "body not found"`)。

#### `physics.is_active(body_id)`

Checks if a physics body is active.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `body_id` | `integer` | `uint32_t` (via `luaL_checkinteger` + `static_cast<uint32_t>`) | Body identifier |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `active` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the body is active |

失败（物理未初始化）返回 `nil, "physics not initialized"`。

#### `physics.ray_cast(ox, oy, oz, dx, dy, dz, max_dist)`

Performs a ray cast against the physics world.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `ox` | `number` | `double` (via `luaL_checknumber`) | Ray origin X |
| `oy` | `number` | `double` (via `luaL_checknumber`) | Ray origin Y |
| `oz` | `number` | `double` (via `luaL_checknumber`) | Ray origin Z |
| `dx` | `number` | `double` (via `luaL_checknumber`) | Ray direction X |
| `dy` | `number` | `double` (via `luaL_checknumber`) | Ray direction Y |
| `dz` | `number` | `double` (via `luaL_checknumber`) | Ray direction Z |
| `max_dist` | `number` | `float` (via `luaL_checknumber` + `static_cast<float>`) | Maximum cast distance |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `hit` | `table` or `nil` | Lua table (via `lua_newtable`) | Hit info table with fields: `body_id` (integer), `x` (number), `y` (number), `z` (number)。无碰撞时返回 `nil`。 |

#### `physics.save_state()`

Saves the current physics state to a binary blob.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `data` | `string` | `lua_pushlstring` | Binary state data for later restore |

失败（物理未初始化）返回 `nil, "physics not initialized"`。

#### `physics.restore_state(data)`

Restores physics state from a previously saved binary blob.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring` → `std::string`) | Binary state data from `save_state()` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

失败返回 `nil, "restore_state failed"`。

#### `physics.recover([saved_state])`

Recovers the physics system, optionally from a saved state.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `saved_state` | `string` | `const char*` + `size_t` (via `lua_tolstring` → `std::string`) | Optional saved state data。如果提供，从该状态恢复；否则执行默认恢复。 |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

失败返回 `nil, "recovery failed"`。

#### `physics.get_stats()`

Returns current physics simulation statistics.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `stats` | `table` | Lua table (via `lua_newtable`) | Stats table with fields: `bodies` (integer — total bodies), `active` (integer — active bodies), `collisions` (integer — contact constraints) |

失败（物理未初始化）返回 `nil, "physics not initialized"`。

### Physics Lua Log Functions

The physics bindings also register `log_trace`, `log_debug`, `log_info`, `log_warn`, `log_error`, `log_fatal` as **global** functions (via `vm.RegisterFunctions`). These route through the physics thread's logger with `[physics_lua]` prefix.

**重要：** 如果 physics bindings 在 log bindings **之后**导出，这些同名全局函数会**覆盖**主日志系统的 `log_*` 函数，此后所有 Lua `log_info()` 等调用将使用 physics 线程 logger。如果 physics 线程未启动，日志可能丢失。建议仅在 physics 线程上的 Lua 脚本中使用这些函数，主线程脚本应使用主日志系统。

Conditionally compiled — 仅在定义 `ENGINE_PHYSICS_ENABLED` 时可用。

### Example

```lua
-- Spawn a body
local body_id = physics.spawn("box", 0, 0, 10, 0, 0, 0, 1, 42)
-- body_id is placeholder (0); real ID comes from next frame result

-- Apply force
physics.apply_force(body_id, 0, 0, -100, 0, 0, 5)

-- Set velocity
physics.set_velocity(body_id, 10, 0, 0)

-- Read transform
local x, y, z, qx, qy, qz, qw = physics.get_transform(body_id)
if x then
    log_info(string.format("Body %d at (%.2f, %.2f, %.2f)", body_id, x, y, z))
end

-- Read velocity
local vx, vy, vz = physics.get_velocity(body_id)
if vx then
    log_info(string.format("Velocity: (%.2f, %.2f, %.2f)", vx, vy, vz))
end

-- Ray cast
local hit = physics.ray_cast(0, 0, 50, 0, 0, -1, 100)
if hit then
    log_info(string.format("Hit body %d at (%.2f, %.2f, %.2f)", hit.body_id, hit.x, hit.y, hit.z))
end

-- Save and restore state
local snapshot = physics.save_state()
-- ... later ...
physics.restore_state(snapshot)

-- Stats
local stats = physics.get_stats()
log_info(string.format("Bodies: %d active / %d total, collisions: %d",
    stats.active, stats.bodies, stats.collisions))

-- Destroy
physics.destroy(body_id)
```

## Notes

- Physics is conditionally compiled with `ENGINE_PHYSICS_ENABLED`
- Internal module access is guarded by `PHYSICS_INTERNAL_ACCESS`
- The fixed timestep is configured separately from the render frame rate
- Lua physics bindings register both the `physics` module table and global `log_*` functions (physics-thread variant)
- Commands (spawn/destroy/apply_force/set_velocity) are asynchronous — they enqueue to the physics thread and return immediately
- Queries (get_transform/get_velocity/is_active/ray_cast/get_stats) read from the most recent physics frame result
- Physics Lua logging (`log_info` etc from physics bindings) uses the physics thread logger; may differ from main thread logging if physics runs on a separate thread
