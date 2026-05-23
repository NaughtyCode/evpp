# JoltPhysics 技术分析报告

基于 JoltPhysics v5.5.1 (master @ 853c282) 的深度源码分析。

## 报告索引

| 编号 | 文件 | 内容 |
|------|------|------|
| 00 | [overview](00-overview.md) | 架构总览、模块划分、设计原则 |
| 01 | [math-layer](01-math-layer.md) | SIMD 向量/矩阵/四元数、双精度架构 |
| 02 | [collision-detection](02-collision-detection.md) | BroadPhase + NarrowPhase、GJK/EPA、CCD |
| 03 | [shape-system](03-shape-system.md) | 13 种形状类型、装饰器模式、复合形状 |
| 04 | [body-system](04-body-system.md) | 刚体管理、运动属性、睡眠检测 |
| 05 | [constraints](05-constraints.md) | 12 种约束、Sequential Impulse 求解、Island Build |
| 06 | [physics-pipeline](06-physics-pipeline.md) | 完整仿真流程、阶段并行化、物理设置 |
| 07 | [character-controller](07-character-controller.md) | Character/Virtual、台阶检测、滑动算法 |
| 08 | [vehicle-system](08-vehicle-system.md) | 悬挂、轮胎摩擦、引擎传动 |
| 09 | [job-system](09-job-system.md) | 作业系统、线程池、Island 并行化 |
| 10 | [memory-serialization](10-memory-serialization.md) | 内存分配、缓存布局、序列化 |
| 11 | [auxiliary-systems](11-auxiliary-systems.md) | 软体/Ragdoll/Skeleton/Hair、GPU Compute、调试 |
