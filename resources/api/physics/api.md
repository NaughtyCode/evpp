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

C++ API via `PhysicsCommand` structures. Not directly exposed to Lua — physics interactions go through higher-level game abstractions.

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

## Notes

- Physics is conditionally compiled with `ENGINE_PHYSICS_ENABLED`
- Internal module access is guarded by `PHYSICS_INTERNAL_ACCESS`
- The fixed timestep is configured separately from the render frame rate
