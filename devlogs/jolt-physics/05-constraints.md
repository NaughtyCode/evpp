# Constraints System 深度分析

## 约束类型

Jolt 提供 12 种内置约束 + 用户可扩展。

### 两体约束 (TwoBodyConstraint)

| 约束子类型 | 自由度数 | 描述 |
|-----------|---------|------|
| Fixed | 0 | 完全锁定两刚体 |
| Point | 3 | 球关节 (共享一个点) |
| Hinge | 1 | 铰链 (绕单轴旋转) |
| Slider | 1 | 滑轨 (沿轴平移) |
| Cone | 2 | 锥形约束 (限制偏离角度) |
| SwingTwist | 1 | 摆动+扭转 (人体关节) |
| SixDOF | 0-6 | 通用6自由度 (可独立锁定) |
| Distance | 1 | 固定/范围距离 |
| Path | 1 | 路径跟随 |
| Gear | - | 齿轮联动 |
| RackAndPinion | - | 齿条齿轮 |
| Pulley | - | 滑轮联动 |

### 约束空间 (EConstraintSpace)

- **LocalToBodyCOM**: 参数在各自刚体的 COM 局部空间中定义
- **WorldSpace**: 参数在世界空间中定义

### 车辆约束 (VehicleConstraint)

独立类别，模拟完整车辆物理（见 [车辆系统](08-vehicle-system.md)）。

## 约束求解架构

### 求解器组成

每个约束分解为若干 `ConstraintPart`：

```
Constraint
  ├── AxisConstraintPart          // 单轴约束 (1 DOF)
  ├── DualAxisConstraintPart      // 双轴约束 (2 DOF)
  ├── AngleConstraintPart         // 角度约束 (1 DOF)
  ├── PointConstraintPart         // 点约束 (3 DOF)
  ├── RotationEulerConstraintPart // 旋转约束 - 欧拉角 (3 DOF)
  ├── RotationQuatConstraintPart  // 旋转约束 - 四元数 (3 DOF)
  ├── HingeRotationConstraintPart // 铰链旋转约束 (1 DOF)
  ├── SwingTwistConstraintPart    // 摆动-扭转约束
  ├── SpringPart                  // 弹簧阻尼
  ├── GearConstraintPart          // 齿轮联动
  ├── RackAndPinionConstraintPart // 齿条齿轮联动
  ├── IndependentAxisConstraintPart // 独立轴约束 (滑轨/滑轮等)
  └── ContactConstraintPart       // 接触约束 (内部使用)
```

### 速度约束求解 (Velocity Solver)

使用 Sequential Impulse (迭代冲量法)：

1. 对每个约束，计算 Jacobian J 和有效质量 M⁻¹
2. 计算约束违反速度
3. 若违反，施加冲量修正: `Δv = M⁻¹ × J × λ`
4. 对 Baumgarte 稳定化项 + 摩擦力额外处理
5. 预热启动 (warm starting): 使用前帧求解的 λ 作为初始值

```cpp
// 经典 Sequential Impulse 步骤
for each constraint:
    effective_mass = 1 / (J * M_inv * Jᵀ)
    lambda = -effective_mass * (J * v + bias)
    lambda = clamp(lambda_accumulated + lambda, limit_range)
    delta_lambda = lambda - lambda_accumulated
    v += M_inv * Jᵀ * delta_lambda
```

### 位置约束求解 (Position Solver)

类似速度求解，但直接修正位置：
- 使用 Baumgarte 稳定化
- 迭代投影穿透修正 (受 `mMaxPenetrationDistance` 限制)
- 迭代次数由 `mNumVelocitySteps` / `mNumPositionSteps` 控制

### 求解优先级
```cpp
mNumVelocityStepsOverride / mNumPositionStepsOverride
```
可对单个刚体覆盖全局迭代次数，用于确保重要约束（如玩家角色）优先满足。

## ConstraintManager

管理所有非接触约束：

```cpp
class ConstraintManager {
    // 添加/移除
    void Add(Constraint**, int);
    void Remove(Constraint**, int);

    // 求解阶段
    void SetupVelocityConstraints(...);
    void SolveVelocityConstraints(...);
    void SolvePositionConstraints(...);
};
```

## ContactConstraintManager

处理接触约束（触碰产生的约束）：

```cpp
class ContactConstraintManager {
    // 接触点管理
    void AddContactConstraint(...);
    void RemoveContactConstraint(...);

    // 摩擦力组合函数
    CombineFunction mCombineFriction;     // 默认: sqrt(f1*f2)
    CombineFunction mCombineRestitution;  // 默认: max(r1, r2)

    // 接触求解 (最多若干个接触点)
    static const int MaxContactPoints = 4;
};
```

### 接触持久化

- Warm starting: 帧间保持接触约束的 lambda 值
- BodyPair 缓存: 缓存上一帧的接触对
- 接触丢失时触发 `ContactListener::OnContactRemoved`

## 约束优先级

`Constraint::mConstraintPriority` 控制求解优先级 (数值越大越优先)：
- 高优先级约束在求解器迭代中先处理
- 用于确保玩家控制器等关键约束正确满足

## Island Builder

构建约束图 → 识别不连通的物理岛：

```
Body A ──Fixed── Body B ──Hinge── Body C
Body D ──Point── Body E

Island 1: {A, B, C}
Island 2: {D, E}
```

每个 Island 可独立并行求解。

### LargeIslandSplitter

当单个 Island 过大（超过并行粒度），将其切分为子 Island 并行求解：
- 使用图染色算法
- 子 Island 间可能有约束依存，通过多次迭代收敛

## ContactListener

接触事件回调：

```cpp
class ContactListener {
    // 验证 (过滤接触对)
    ValidateResult OnContactValidate(const Body&, const Body&, RVec3Arg, const CollideShapeResult&);

    // 添加 (新接触)
    void OnContactAdded(const Body&, const Body&, const ContactManifold&, ContactSettings&);

    // 保持 (持续接触)
    void OnContactPersisted(const Body&, const Body&, const ContactManifold&, ContactSettings&);

    // 移除 (接触分离)
    void OnContactRemoved(const SubShapeIDPair&);
};
```

## 增强内部边去除 (Enhanced Internal Edge Removal)

解决三角网格内部边的虚假碰撞问题：
- 检测内部边 (相邻面夹角小于阈值)
- 对内部边禁用碰撞响应
- 通过 `BodyCreationSettings::mEnhancedInternalEdgeRemoval` 启用
