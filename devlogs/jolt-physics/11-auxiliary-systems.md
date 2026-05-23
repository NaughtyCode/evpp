# 辅助系统分析

## SoftBody (软体)

### 软体架构
```
SoftBodyCreationSettings
  ├── SoftBodyShape          (Shape 子类型)
  ├── SoftBodyMotionProperties
  └── SoftBodySharedSettings (共享配置)
```

### 算法
基于 **XPBD** (Extended Position Based Dynamics, Matthias Müller, Ten Minute Physics)。内部通过多次迭代 (`mNumIterations`, 默认 5) 在子步内求解约束。

### 约束类型
- Dihedral Bend (二面角弯曲约束)
- Volume (体积保持约束)
- Skin (蒙皮约束)
- Edge (边长度保持约束)
- Rod Stretch/Shear (杆伸缩/剪切约束)
- Rod Bend/Twist (杆弯曲/扭转约束)
- 碰撞约束 (与刚体摩擦接触)
- 传感器碰撞

### 集成
在 `PhysicsSystem::Update` 中有专门的软体步骤：
```
JobSoftBodyPrepare   // 准备顶点数据
JobSoftBodyCollide   // 碰撞检测
JobSoftBodySimulate  // 执行模拟
JobSoftBodyFinalize  // 更新状态
```

## Ragdoll (布娃娃)

`Jolt/Physics/Ragdoll/`:
```cpp
class Ragdoll {
    // 基于骨骼的布娃娃
    // 自动创建约束连接各部分
    // 映射骨骼到物理刚体
};
```

驱动方式：
- **Motor**: 主动驱向目标姿态
- **Stiffness**: 刚度控制关节角度

## Skeleton (骨骼)

`Jolt/Skeleton/`:
- 骨骼层级管理
- 姿态计算
- 与 Ragdoll 系统集成

## Hair (头发模拟)

`Jolt/Physics/Hair/`:
- 基于链式粒子的头发模拟
- 段约束 (segment constraints)
- 与刚体的碰撞交互

## Geometry 工具

| 文件 | 功能 |
|------|------|
| `ConvexHullBuilder.h` | 3D 凸包构建 |
| `ConvexHullBuilder2D.h` | 2D 凸包构建 |
| `Indexify.h` | 顶点去重/索引化 |
| `ClosestPoint.h` | 最近点计算 |
| `ClipPoly.h` | 多边形裁剪 |
| `AABox.h` / `AABox4.h` | AABB (单/四个) |
| `Sphere.h` | 球体 |
| `Triangle.h` | 三角形 |
| `Plane.h` | 平面 |
| `OrientedBox.h` | 有向包围盒 |

## AABBTree

`Jolt/AABBTree/`:
- 自底向上 AABB 树构建
- 支持 half-float 节点压缩
- 多种三角编码格式 (SoA4, 8-bit 索引)
- 将构建结果写入 buffer

## Compute (GPU 后端)

`Jolt/Compute/` 支持将物理部分卸载到 GPU：

| 后端 | 支持平台 |
|------|---------|
| CPU | 所有 (回退) |
| DX12 (DirectX 12) | Windows |
| MTL (Metal) | macOS/iOS |
| VK (Vulkan) | 跨平台 |

### 抽象接口
```cpp
class ComputeSystem { /* 创建 Shader/Buffer/Queue */ };
class ComputeShader { /* 执行 Compute Kernel */ };
class ComputeBuffer { /* GPU 缓冲区 */ };
class ComputeQueue   { /* 命令队列 */ };
```

### Shader 编译
HLSL shader → CPU C++ (通过 `HLSLToCPP.h`) 进行 CPU 回退执行。

## Debug Renderer

`Jolt/Renderer/` 和 `Jolt/Physics/DebugRenderer`:
- 调试线框绘制
- 支持所有形状类型
- 碰撞点/法线可视化
- 约束参考帧绘制
- 通过 `JPH_DEBUG_RENDERER` 宏控制编译

## Profiler

`Jolt/Core/Profiler.h`:
- 编译时控制 (`JPH_PROFILE_ENABLED`)
- 支持外部 profiler (`JPH_EXTERNAL_PROFILE`)
- `JPH_PROFILE(...)` 宏
- 模拟统计追踪 (`JPH_TRACK_SIMULATION_STATS`)

## Determinism Log

`Jolt/Physics/DeterminismLog.h`:
- 用于跨平台确定性验证
- 记录关键中间值
- 通过 `JPH_DET_LOG(...)` 宏写入

## HelloWorld

`HelloWorld/HelloWorld.cpp`: 最小化示例，约 100 行代码演示：
1. 注册分配器
2. 创建 PhysicsSystem
3. 创建地面 + 球体
4. 运行更新循环
5. 输出位置

用于验证编译和基本集成。
