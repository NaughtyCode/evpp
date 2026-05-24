# JoltPhysics 引擎总览

## 项目信息

- **版本**: v5.5.1
- **作者**: Jorrit Rouwe
- **许可**: MIT
- **语言**: C++17，约 200+ 头文件 + 100+ 源文件
- **定位**: 面向游戏的工业级多线程刚体物理引擎

## 架构分层

```
┌─────────────────────────────────────────────────────┐
│  Application Layer                                  │
│  (BodyInterface, PhysicsSystem API)                 │
├─────────────────────────────────────────────────────┤
│  High-Level Systems                                 │
│  Character  │  Vehicle  │  Ragdoll  │  SoftBody    │
├─────────────────────────────────────────────────────┤
│  Simulation Pipeline                                │
│  BroadPhase → NarrowPhase → IslandBuilder → Solver │
├─────────────────────────────────────────────────────┤
│  Core Physics                                       │
│  Body  │  Shape  │  Constraint  │  Collision       │
├─────────────────────────────────────────────────────┤
│  Foundation                                         │
│  Math (SIMD) │  Core │  Geometry │  ObjectStream   │
├─────────────────────────────────────────────────────┤
│  Platform                                          │
│  SSE/AVX/NEON/RVV │ Memory │ JobSystem │ Compute   │
└─────────────────────────────────────────────────────┘
```

## 核心设计原则

### 1. 确定性 (Determinism)
- 支持跨平台确定性物理 (`JPH_CROSS_PLATFORM_DETERMINISTIC`)
- 通过 `BodyID` 显式分配实现网络同步
- 状态保存/恢复 (`SaveState`/`RestoreState`)

### 2. 数据导向 (Data-Oriented)
- 热数据缓存行对齐 (cache line: 64 bytes)
- SoA 布局，连续内存存储避免 cache miss
- SIMD 友好 (Vec4/Vec3 16 字节对齐，Mat44 使用 4 个 SIMD 向量)

### 3. 多线程作业 (Job System)
- 内置 `JobSystemThreadPool` 线程池
- Island 并行化: 无关联的物理岛可并行求解
- Large Island Splitter: 将大岛切分为可并行的子组

### 4. 层架构 (Layer Architecture)
- **ObjectLayer** (uint16): 应用自定义碰撞层
- **BroadPhaseLayer** (uint8): 内部粗检测层
- 通过 `BroadPhaseLayerInterface` 映射二者

## 模块概览

| 模块 | 源码路径 | 功能 |
|------|----------|------|
| Math | `Jolt/Math/` | SIMD 向量/矩阵/四元数 |
| Core | `Jolt/Core/` | 内存、作业、工厂、序列化 |
| Geometry | `Jolt/Geometry/` | AABB、凸包、最近点 |
| Physics/Body | `Jolt/Physics/Body/` | 刚体管理、运动属性 |
| Physics/Collision | `Jolt/Physics/Collision/` | 碰撞检测、形状系统 |
| Physics/Constraints | `Jolt/Physics/Constraints/` | 约束求解器 |
| Physics/Character | `Jolt/Physics/Character/` | 角色控制器 |
| Physics/Vehicle | `Jolt/Physics/Vehicle/` | 车辆物理 |
| Physics/SoftBody | `Jolt/Physics/SoftBody/` | 软体物理 |
| Physics/Ragdoll | `Jolt/Physics/Ragdoll/` | 布娃娃系统 |
| Physics/Hair | `Jolt/Physics/Hair/` | 头发模拟 |
| AABBTree | `Jolt/AABBTree/` | 加速结构构建 |
| Compute | `Jolt/Compute/` | GPU 计算后端 (DX12/VK/MTL) |
| ObjectStream | `Jolt/ObjectStream/` | 二进制/文本序列化 |
| Skeleton | `Jolt/Skeleton/` | 骨骼动画 |
| Renderer | `Jolt/Renderer/` | 调试渲染器 |

## 关键技术指标

- **形状类型**: 18 种内置形状 + 16 种用户自定义 (含凸包、三角网格、高度场、装饰器等)
- **约束类型**: 12 种两体约束 + 接触约束
- **运动类型**: Static / Kinematic / Dynamic
- **碰撞检测**: 粗检测 (QuadTree) + 窄检测 (GJK/EPA for convex, 自定义 for mesh)
- **连续碰撞 (CCD)**: LinearCast 模式，防止高速穿透
- **精度**: 单精度 (默认) / 双精度 (`JPH_DOUBLE_PRECISION`)
- **最大刚体数**: ~8.3M (BodyID::cMaxBodyIndex = 0x7fffff = 23 位索引)
