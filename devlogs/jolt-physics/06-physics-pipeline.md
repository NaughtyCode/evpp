# Physics Simulation Pipeline 深度分析

## Update() 主循环

`PhysicsSystem::Update(deltaTime, collisionSteps, tempAllocator, jobSystem)` 将时间步分为 `collisionSteps` 个子步，每个子步执行完整的碰撞检测 + 积分循环。

## 每个碰撞步 (Step) 的实际执行顺序

基于 `PhysicsUpdateContext::Step` 中的 JobHandle 分析：

```
1.  BroadPhase::UpdatePrepare (后台构建新树)
2.  StepListeners (通知监听器)
3.  DetermineActiveConstraints  (标记活跃约束)
4.  ApplyGravity                (重力作用于速度)
5.  FindCollisions              (BroadPhase + NarrowPhase)
6.  UpdateBroadphaseFinalize    (交换新/旧 broadphase 树)
7.  SetupVelocityConstraints    (准备约束求解数据)
8.  BuildIslandsFromConstraints (构建约束岛)
9.  FinalizeIslands             (合并接触约束入岛)
10. BodySetIslandIndex          (设置刚体岛索引)
11. SolveVelocityConstraints    (速度约束求解)
12. PreIntegrateVelocity        (积分前回调)
13. IntegrateVelocity           (Euler 积分)
14. PostIntegrateVelocity       (积分后回调)
15. ResolveCCDContacts          (连续碰撞检测解析)
16. SolvePositionConstraints    (位置约束求解)
17. ContactRemovedCallbacks     (接触移除回调)
18. SoftBodyPrepare/Collide/Simulate/Finalize (软体步骤)
19. StartNextStep               (触发下一个子步)
```

每个子步结束后，在 `CheckSleepAndUpdateBounds` 中：
- 更新 BroadPhase AABB
- 运行睡眠检测 (SleepTestSpheres)
- 符合条件的刚体进入睡眠状态

## 各阶段详解

### Step 1: DetermineActiveConstraints

扫描所有约束：涉及活跃刚体的约束 → 标记参与求解。休眠刚体的约束跳过。批量大小: `cDetermineActiveConstraintsBatchSize = 64`。

### Step 2: ApplyGravity

对每个活跃动态刚体 (batch=64)：
```
linear_velocity += gravity * gravityFactor * deltaTime
```
被 `AllowedDOFs` 锁定的分量不受影响。

在此之前，若 `mApplyGyroscopicForce` 开启，先对刚体施加陀螺力 (Dzhanibekov effect):
`torque += ω × (I·ω)` — 网球拍定理，默认关闭。

### Step 3: FindCollisions (核心碰撞检测)

作业并行执行，从 BroadPhase 获取 BodyPair → NarrowPhase 检测：

1. 遍历 BodyPair 队列 (多生产者)
2. 检查 ObjectLayer 碰撞过滤
3. `CollisionDispatch::sCollideShapeVsShape` 精确碰撞
4. 收集 `CollideShapeResult` → 创建 `ContactConstraint`
5. 若接触约束数超过限制 → 跳过后续对
6. 动态生成新 FindCollisions 作业直到达到最大并发

### Step 4: UpdateBroadphaseFinalize

将后台构建的新 broadphase 树切换为当前树。

### Step 5: SetupVelocityConstraints

初始化约束求解数据 (batch=256)：
- 计算 Jacobian J
- 计算有效质量矩阵逆
- 加载 warm start lambda (前帧冲量)
- 初始化弹簧/阻尼项

### Step 6: BuildIslandsFromConstraints + FinalizeIslands

构建约束连通图：
- 并查集 (Union-Find) 将约束连接的刚体合并
- 接触约束连接的对也合并入岛
- `IslandBuilder` 分配岛编号
- 最终确定各岛的刚体/约束列表

### Step 7: SolveVelocityConstraints

对每个 Island 并行求解 Sequential Impulse：
- 迭代 `mNumVelocitySteps` 次
- 高优先级约束先解
- 应用摩擦力冲量 (依赖上一次迭代的反法向冲量 → 需要 ≥ 2 次迭代)
- 应用弹性恢复
- 夹紧速度到 `mMaxLinearVelocity` / `mMaxAngularVelocity`

求解后 (在 CheckSleepAndUpdateBounds 中)：
- 更新 BroadPhase AABB
- 检查睡眠条件

### Step 8: PreIntegrateVelocity

`StepListener::OnStep` 回调 (积分前)。车辆等系统在此应用悬挂力。

### Step 9: IntegrateVelocity

Euler 半隐式积分 (batch=64)：
```
linear_velocity  += (force * invMass) * deltaTime
angular_velocity += torque * invInertia * deltaTime

position += linear_velocity * deltaTime
rotation += 0.5 * quat(0, angular_velocity) * rotation * deltaTime
```

### Step 10: PostIntegrateVelocity

- `StepListener::OnStep` 回调 (积分后)
- 应用阻尼: `velocity *= 1.0 - damping * deltaTime`

### Step 11: ResolveCCDContacts

对 `EMotionQuality::LinearCast` 的刚体 (batch=4)：
1. 沿 Δposition 扫掠检测
2. 若穿透 → 回退到 TOI (首次接触时间)
3. 反射速度 → 继续移动剩余时间

CCD 阈值由 `mLinearCastThreshold` (默认 0.75 × 内接球半径) 和 `mLinearCastMaxPenetration` (默认 0.25 × 内接球半径) 控制。

### Step 12: SolvePositionConstraints

NGS (Nonlinear Gauss-Seidel) 位置修正：
- 迭代 `mNumPositionSteps` 次
- 修正穿透 (受 `mMaxPenetrationDistance` 限制，默认 0.2m)
- Baumgarte 稳定化系数 (默认 0.2)

### Step 13: ContactRemovedCallbacks

对消失的接触对触发 `ContactListener::OnContactRemoved`。

## 并行化策略

### Island 并行
不同 Island 完全独立求解 → 岛级并行作业。

### 作业粒度
- `cActiveBodiesBatchSize = 16`: 活跃刚体批处理
- `cNarrowPhaseBatchSize = 16`: 窄碰撞批次
- `cIntegrateVelocityBatchSize = 64`: 积分批次
- `cNumCCDBodiesPerJob = 4`: CCD 批次
- `cSetupVelocityConstraintsBatchSize = 256`: 约束准备批次
- `cDetermineActiveConstraintsBatchSize = 64`: 活跃约束扫描批次

### LargeIslandSplitter
当单个 Island 过大 (如一堆相连刚体):
1. 图染色分割为子组
2. 每个子组独立求解
3. 边界约束通过多次迭代收敛
4. 可通过 `mUseLargeIslandSplitter` 开关 (默认开启)

## TempAllocator

帧级临时内存分配器：
- Update() 期间所有临时分配从此获取
- Update() 结束时全部释放
- 避免每帧 malloc/free
- `PhysicsUpdateContext` 内部使用 `STLTempAllocator`

## PhysicsSettings 完整字段

```cpp
struct PhysicsSettings {
    // 碰撞检测
    int    mMaxInFlightBodyPairs = 16384;                  // 飞行中最大 BodyPair 数
    float  mSpeculativeContactDistance = 0.02f;            // 推测接触距离
    float  mPenetrationSlop = 0.02f;                       // 允许穿透容差
    float  mManifoldTolerance = 1.0e-3f;                   // 流形公差
    float  mBodyPairCacheMaxDeltaPositionSq = 1e-6f;       // 接触缓存位置阈值
    float  mBodyPairCacheCosMaxDeltaRotationDiv2 = 0.9998f;// 接触缓存旋转阈值 (cos 2°)
    float  mContactNormalCosMaxDeltaRotation = 0.9962f;    // 合并流形法线阈值 (cos 5°)
    float  mContactPointPreserveLambdaMaxDistSq = 1e-4f;   // 接触点保留阈值

    // 求解器
    uint   mNumVelocitySteps = 10;                         // 速度迭代 (≥ 2 以支持摩擦力)
    uint   mNumPositionSteps = 2;                          // 位置迭代
    float  mBaumgarte = 0.2f;                              // 位置修正系数
    float  mMinVelocityForRestitution = 1.0f;              // 弹性恢复最小速度

    // 睡眠
    float  mTimeBeforeSleep = 0.5f;                        // 静止超时 (秒)
    float  mPointVelocitySleepThreshold = 0.03f;           // 睡眠速度阈值 (m/s)

    // CCD
    float  mLinearCastThreshold = 0.75f;                   // 触发 CCD 的位置阈值 (×内接半径)
    float  mLinearCastMaxPenetration = 0.25f;              // CCD 允许最大穿透 (×内接半径)

    // 位置修正
    float  mMaxPenetrationDistance = 0.2f;                 // 单次迭代最大修正距离

    // StepListener
    int    mStepListenersBatchSize = 8;
    int    mStepListenerBatchesPerJob = 1;

    // 开关
    bool   mDeterministicSimulation = true;                // 确定性模拟
    bool   mConstraintWarmStart = true;                    // 约束预热启动
    bool   mUseBodyPairContactCache = true;                // BodyPair 缓存
    bool   mUseManifoldReduction = true;                   // 流形归约
    bool   mUseLargeIslandSplitter = true;                 // 大岛分割
    bool   mAllowSleeping = true;                          // 允许睡眠
    bool   mCheckActiveEdges = true;                       // 检查活跃边 (内部边去除)

    // 内部边去除
    float  mInternalEdgeRemovalVertexToleranceSq = 1.0e-8f;
};
```

## StepListener

```cpp
class PhysicsStepListener {
    virtual void OnStep(float inDeltaTime, PhysicsSystem*) = 0;
};
```

车辆等系统通过注册 StepListener 在积分前后执行逻辑 (悬挂力、引擎更新等)。
