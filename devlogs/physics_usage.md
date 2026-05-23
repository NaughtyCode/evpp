# 物理引擎系统 — 使用文档

> 基于 JoltPhysics v5.5.1 | 实施日期: 2026-05-23

## 目录

1. [快速入门](#快速入门)
2. [编译与构建](#编译与构建)
3. [配置文件详解](#配置文件详解)
4. [资产文件格式](#资产文件格式)
5. [Lua API 参考](#lua-api-参考)
6. [C++ API 参考](#c-api-参考)
7. [调试与故障排查](#调试与故障排查)
8. [版本与兼容性](#版本与兼容性)

---

## 快速入门

### 系统概述

引擎物理系统是一个独立的物理模拟子系统，基于 JoltPhysics 构建。核心架构：

```
主线程 ── Spawn/Tick 命令 ──► 物理线程
  │                              │
  │  · 游戏逻辑                  │  · 场景管理
  │  · 网络同步                  │  · 物理步进 (Jolt Update)
  │  · 物理专用 ScriptVM         │  · 变化提取 (Diff)
  │                              │
  ◄── PhysicsFrameResult ────────┘
```

### 最小可运行示例

```cpp
// 1. 初始化（Engine::Init 中自动调用）
PhysicsEngineBridge::Instance().Initialize(
    "resources/physics/configs",   // 配置文件目录
    "resources/physics/data",      // 资产文件目录
    "resources/script"             // Lua 脚本目录
);

// 2. 启动物理模拟（上层在适当时机显式调用）
PhysicsEngineBridge::Instance().Start();

// 3. 每帧 Tick
PhysicsEngineBridge::Instance().Tick(frame_id, fixed_delta_time);

// 4. 获取本帧结果
auto result = PhysicsEngineBridge::Instance().FetchResult(frame_id, 5);
if (result) {
    // result->transforms        — 所有活跃刚体的变换快照
    // result->collision_events  — 本帧碰撞事件
    // result->diff_packets      — 变化增量数据

    // 5. 触发碰撞回调
    PhysicsEngineBridge::Instance().UpdateScript();
}

// 6. 关闭
PhysicsEngineBridge::Instance().Shutdown();
```

### 生命周期

1. `Initialize()` — 加载配置 + 创建 ScriptVM + 加载 Lua 脚本。**不启动物理线程**。
2. `Start()` — 显式启动物理线程，物理模拟开始。
3. 运行循环 — `Tick()` → 非物理逻辑 → `FetchResult()` → 碰撞回调。
4. `Shutdown()` — 停止物理线程 + 销毁 ScriptVM。

---

## 编译与构建

### ENGINE_PHYSICS_ENABLED 宏

| 构建类型 | 宏状态 | 行为 |
|----------|--------|------|
| 服务器构建 | 定义 | engine/physics/ 全部源文件参与编译，物理系统完全可用 |
| 客户端构建 | 未定义 | engine_physics 为空 INTERFACE 库，所有调用编译为空操作 |

### CMake 配置

```cmake
# 服务器构建（默认）
cmake -DENGINE_PHYSICS_ENABLED=ON ..

# 客户端构建
cmake -DENGINE_PHYSICS_ENABLED=OFF ..
```

### 依赖库

| 库 | 用途 | 路径 |
|----|------|------|
| JoltPhysics v5.5.1 | 物理引擎核心 | 3rdparty/JoltPhysics |
| moodycamel::ConcurrentQueue | SPSC 无锁队列 | 3rdparty/concurrentqueue |
| glaze | JSON 序列化 | 3rdparty/glaze |
| quill | 日志系统 | 3rdparty/quill |
| Lua 5.4 | 脚本引擎 | 3rdparty/lua |

---

## 配置文件详解

配置文件目录: `resources/physics/configs/`

### physics.json — 物理引擎核心参数

| 字段 | 类型 | 默认值 | 说明 | 热更新 |
|------|------|--------|------|--------|
| `gravityX/Y/Z` | float | 0/-9.81/0 | 重力向量 (m/s²) | 否 |
| `fixedDeltaTime` | float | 0.01667 | 固定步长 (60Hz) | 否 |
| `solverIterations` | int | 10 | 速度求解器迭代次数 | 否 |
| `subStepCount` | int | 2 | 每帧子步数 | 否 |
| `maxBodies` | int | 4096 | 最大刚体数 | 否 |
| `maxContactPoints` | int | 10240 | 最大接触点数 | 否 |
| `maxBodyPairs` | int | 16384 | 最大 BodyPair 数 | 否 |
| `numBodyMutexes` | int | 32 | Body 互斥锁数 (0=auto) | 否 |
| `deterministicSimulation` | bool | true | 确定性模拟 | 否 |
| `allowSleeping` | bool | true | 允许睡眠 | 否 |

完整字段列表见配置文件内 `_comment` 注释。

### threading.json — 线程模型配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `threadPriority` | string | "high" | 物理线程优先级 |
| `affinityMask` | uint64 | 0 | CPU 亲和性掩码 |
| `commandQueueSize` | int | 256 | 命令队列容量 |
| `resultQueueSize` | int | 64 | 结果队列容量 |
| `maxPendingFrames` | int | 3 | 最大待处理帧数 |
| `jobSystemMaxJobs` | int | 2048 | JobSystem maxJobs |
| `jobSystemMaxBarriers` | int | 8 | JobSystem maxBarriers ([1,8]) |
| `jobSystemThreadCount` | int | -1 | 工作线程数 (-1=auto) |

### logging.json — 日志配置

| 字段 | 类型 | 默认值 | 说明 | 热更新 |
|------|------|--------|------|--------|
| `logDir` | string | "./logs/physics" | 日志目录 | 否 |
| `fileName` | string | "physics_engine" | 日志文件基础名 | 否 |
| `level` | string | "info" | 日志级别 | **是** |
| `maxFileSizeMb` | int | 50 | 单文件最大大小 | 否 |

### thresholds.json — 变化检测阈值

| 字段 | 类型 | 默认值 | 说明 | 热更新 |
|------|------|--------|------|--------|
| `positionEpsilon` | float | 0.001 | 位置阈值 (1mm) | **是** |
| `rotationEpsilon` | float | 0.00017 | 旋转阈值 (~0.01°) | **是** |
| `linearVelocityEpsilon` | float | 0.01 | 线速度阈值 (0.01 m/s) | **是** |
| `angularVelocityEpsilon` | float | 0.001 | 角速度阈值 (0.001 rad/s) | **是** |

### 热更新

```cpp
// 运行时热更新阈值
PhysicsEngineBridge::Instance().ReloadThresholds();

// 运行时热更新日志级别
PhysicsEngineBridge::Instance().ReloadLogLevel();
```

---

## 资产文件格式

### 完整 Schema

```json
{
  "staticBodies": [
    {
      "id": "ground",
      "shape": {
        "type": "box",
        "params": { "halfExtent": [50, 1, 50] }
      },
      "material": { "friction": 0.8, "restitution": 0.1 },
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0, 1]
      },
      "objectLayer": "static"
    }
  ],
  "dynamicPrototypes": [
    {
      "protoId": "crate",
      "shape": {
        "type": "box",
        "params": { "halfExtent": [0.5, 0.5, 0.5] }
      },
      "mass": 10.0,
      "material": { "friction": 0.6, "restitution": 0.2 },
      "motionType": "dynamic",
      "motionQuality": "discrete",
      "linearDamping": 0.1,
      "angularDamping": 0.1,
      "gravityFactor": 1.0,
      "objectLayer": "dynamic",
      "isSensor": false,
      "allowSleeping": true,
      "maxLinearVelocity": 500.0,
      "maxAngularVelocity": 47.1
    }
  ],
  "constraints": [
    {
      "type": "hinge",
      "bodyA": "door",
      "bodyB": "frame",
      "pivot": [0, 1, 0],
      "axis": [0, 1, 0],
      "limits": { "min": -1.57, "max": 1.57 }
    }
  ],
  "materials": [
    { "name": "concrete", "friction": 0.8, "restitution": 0.1 },
    { "name": "ice", "friction": 0.05, "restitution": 0.01 }
  ]
}
```

### 支持的形状类型

| 类型 | 参数 | 说明 |
|------|------|------|
| `box` | `halfExtent: [x, y, z]` | 立方体 |
| `sphere` | `radius: float` | 球体 |
| `capsule` | `halfHeight, radius` | 胶囊体 |
| `cylinder` | `halfHeight, radius` | 圆柱体 |
| `convex_hull` | `points: [[x,y,z], ...]` | 凸包 |
| `mesh` | `vertices, triangles` | 三角网格 |
| `height_field` | `samples, sampleCount, offset, scale` | 高度场 |
| `plane` | `normal, constant` | 无限平面 |
| compound | `shapes: [{type, params, position, rotation}, ...]` | 复合形状 |

### 支持的约束类型

| 类型 | 参数 | 说明 |
|------|------|------|
| `hinge` | `bodyA, bodyB, pivot, axis, limits` | 铰链 |
| `spring` | `bodyA, bodyB, pivot, spring: {frequency, damping}` | 弹簧 |
| `slider` | `bodyA, bodyB, pivot, axis, axis2, limits` | 滑杆 |
| `fixed` | `bodyA, bodyB, pivot` | 固定 |

---

## Lua API 参考

### 物理专用 ScriptVM

物理系统拥有独立的 ScriptVM，与引擎 ScriptVM 完全隔离：
- 物理 VM 加载 `resources/script/` 下所有 `.lua` 文件
- 物理脚本可通过 `require("physics")` 或 `physics.xxx` 访问 API
- 物理脚本无法访问引擎 API（timer、网络等）
- 引擎脚本无法直接访问物理 API

### 命令 API（异步）

```lua
-- 生成动态刚体
local body_id, err = physics.spawn("crate", 10, 0, 5, 0, 0, 0, 1, user_data)

-- 销毁刚体
local ok = physics.destroy(body_id)

-- 施加力（世界坐标）
local ok = physics.apply_force(body_id, 0, 100, 0, 10, 0, 5)

-- 设置线速度
local ok = physics.set_velocity(body_id, 5, 0, 0)
```

### 查询 API（同步）

```lua
-- 获取变换
local x, y, z, qx, qy, qz, qw = physics.get_transform(body_id)

-- 获取线速度
local vx, vy, vz = physics.get_velocity(body_id)

-- 检查活跃状态
local active = physics.is_active(body_id)

-- 射线检测
local hit = physics.ray_cast(0, 0, 0, 0, -1, 0, 100)
-- hit = { body_id, x, y, z } or nil

-- 获取统计
local stats = physics.get_stats()
-- stats = { bodies=N, active=N, collisions=N }
```

### 碰撞回调

```lua
-- 在物理专用脚本中定义此全局函数
function on_physics_collision(event)
    -- event = {
    --   body_a = uint32,
    --   body_b = uint32,
    --   type   = "start" | "persist" | "end",
    --   points = { {x, y, z}, ... }  -- 世界坐标接触点
    -- }
    if event.type == "start" then
        print("collision: " .. event.body_a .. " vs " .. event.body_b)
    end
end
```

### 区分运行上下文

由于引擎 VM 和物理 VM 都加载同一 scripts_dir，脚本应检测运行上下文：

```lua
-- 检查物理模块是否可用
if physics then
    -- 在物理 VM 中运行
    function on_physics_collision(event)
        -- 处理碰撞
    end
end

if timer then
    -- 在引擎 VM 中运行
    timer.set_timeout(1000, function()
        -- 引擎逻辑
    end)
end
```

---

## C++ API 参考

### PhysicsEngineBridge（对外接口）

```cpp
class PhysicsEngineBridge {
public:
    static PhysicsEngineBridge& Instance();

    bool Initialize(const std::string& config_dir,
                    const std::string& assets_path,
                    const std::string& scripts_dir);
    bool Start();
    void Tick(uint64_t frame_id, float delta_time);
    void UpdateScript();
    std::optional<PhysicsFrameResult> FetchResult(uint64_t frame_id, int timeout_ms);
    void Shutdown();

    bool IsRunning() const;
    bool IsHealthy() const;
    ScriptVM* GetScriptVM();
    float GetFixedDeltaTime() const;
};
```

### 帧循环时序

```
Frame N:
  1. Tick(frame_id=N, delta)      — 入队 Tick 命令，触发物理步进
  2. 引擎 Lua 脚本更新             — AI、定时器、聊天等
  3. FetchResult(frame_id=N, 5ms)  — 等待物理结果
  4. UpdateScript()               — 碰撞回调 (on_physics_collision)
  5. 基于 diff_packets 建网络增量  — 网络同步
```

### 错误处理

- `Initialize()` 返回 `false` → 配置加载失败，引擎降级运行
- `FetchResult()` 返回 `nullopt` → 超时，物理线程可能卡住
- `IsHealthy()` 返回 `false` → 物理线程异常退出，需重新初始化

---

## 调试与故障排查

### 日志文件

物理系统使用独立日志实例，日志文件位于:
```
./logs/physics/physics_engine_YYYY-MM-DD.log
```

### 常见问题

**配置加载失败**
- 检查 `resources/physics/configs/` 下 4 个 JSON 文件是否存在
- 检查 JSON 格式是否正确（字段名大小写 sensitive）
- 检查字段值是否在合法范围内

**物理线程崩溃**
- 检查日志中 `PhysicsThread: exception` 错误
- 检查 `maxBodyPairs` 和 `maxContactPoints` 是否足够
- 检查 JoltPhysics 库是否正确链接

**帧堆积**
- 检查日志中 `frame pile-up` 警告
- 增大 `maxPendingFrames` 或降低物理复杂度
- 减小 `maxBodies` 或简化碰撞几何

### 性能调优

- 降低 `solverIterations` (默认 10 → 6-8 可提升性能)
- 启用 `allowSleeping` 和 `useManifoldReduction`
- 简化碰撞几何（用 box/sphere 替代 mesh）
- 减少 `maxBodyPairs` (确保 > maxContactPoints)

---

## 版本与兼容性

### JoltPhysics 版本

- 锁定版本: JoltPhysics v5.5.1 (MIT License)
- 路径: `3rdparty/JoltPhysics`

### 跨平台注意事项

- Windows: 使用 SSE2 SIMD
- Linux: 使用 SSE2 或 NEON
- 确定性: 开启 `deterministicSimulation=true` 和 `JPH_CROSS_PLATFORM_DETERMINISTIC` 编译选项

### 编译宏

| 宏 | 说明 |
|----|------|
| `ENGINE_PHYSICS_ENABLED` | 统一控制物理代码编译 |
| `JPH_CROSS_PLATFORM_DETERMINISTIC` | 禁用 FMA 确保跨平台确定性 |
