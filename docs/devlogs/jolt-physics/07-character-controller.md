# Character Controller 深度分析

## 架构

Jolt 提供两种角色控制器：

| 类型 | 基类 | 特点 |
|------|------|------|
| Character | CharacterBase | 简单，作为动态刚体参与物理 |
| CharacterVirtual | CharacterBase | 高级，不参与物理但检测碰撞 |

### Character

```cpp
class Character : public CharacterBase {
    // 就是一个胶囊形的动态刚体
    // 直接参与物理模拟
    // 使用约束来维持直立姿态
};
```

适用场景：需要与物理世界完全交互的角色（如被物理对象推动）。

### CharacterVirtual

```cpp
class CharacterVirtual : public CharacterBase {
    // 不加入 BroadPhase
    // 手动执行碰撞检测和滑动
    // 通过 ExtendedUpdate 执行移动
};
```

适用场景：纯运动学角色，如玩家控制器。由于不加入 BroadPhase，性能更好且更可控。

### CharacterVirtualSettings

配置类 (`CharacterVirtualSettings` 继承自 `CharacterBaseSettings`)，构造时传入所有参数：

```cpp
// 继承自 CharacterBaseSettings:
Vec3          mUp = Vec3::sAxisY();          // 上方向
Plane         mSupportingVolume { ... };      // 支撑体积
float         mMaxSlopeAngle = DegreesToRadians(50.0f); // 最大可攀爬角度 (存储为 cos)
bool          mEnhancedInternalEdgeRemoval = false;
RefConst<Shape> mShape;                      // 角色形状

// CharacterVirtualSettings 自有:
CharacterID   mID;                           // 确定性 ID
float         mMass = 70.0f;                 // 角色质量 (kg)
float         mMaxStrength = 100.0f;         // 最大推力 (N)
Vec3          mShapeOffset = Vec3::sZero();  // 局部空间形状偏移
float         mPredictiveContactDistance = 0.1f;
uint          mMaxCollisionIterations = 5;
uint          mMaxConstraintIterations = 15;
uint          mMaxNumHits = 256;             // 单次收集的最大接触点数
float         mCharacterPadding = 0.02f;
float         mPenetrationRecoverySpeed = 1.0f;
RefConst<Shape> mInnerBodyShape;             // 可选的内部刚体形状
BodyID        mInnerBodyIDOverride;
ObjectLayer   mInnerBodyLayer = 0;
```

> `CharacterBase` 将 `mMaxSlopeAngle` 转换为 `mCosMaxSlopeAngle = cos(angle)` 存储，用于地面坡度判定。

## CharacterVirtual 核心算法

### 移动流程 ExtendedUpdate

```
1. 预测 (Prediction): 沿期望方向移动
2. 碰撞检测: CastShape 扫掠角色形状
3. 碰撞解析:
   a. 计算滑动平面
   b. 沿平面重新投影剩余移动
4. 迭代: 重复直到剩余移动 < ε 或达到最大迭代
5. 穿插恢复: 若仍有穿透，运行位置约束求解
```

### 关键设置

`CharacterVirtual` 的关键运行时成员变量 (由 `CharacterVirtualSettings` 构造时初始化):

```cpp
// 碰撞检测参数 (由 CharacterVirtualSettings 设置)
EBackFaceMode  mBackFaceMode;               // 背面碰撞模式
float  mPredictiveContactDistance;          // 预判距离
uint   mMaxCollisionIterations;             // 碰撞迭代
uint   mMaxConstraintIterations;            // 约束迭代
float  mMinTimeRemaining;                   // 早停阈值
float  mCollisionTolerance;                 // 碰撞容差
float  mCharacterPadding;                   // 安全边距
uint   mMaxNumHits;                         // 单次最大接触点数
float  mHitReductionCosMaxAngle;            // 接触归并阈值
float  mPenetrationRecoverySpeed;           // 穿透恢复速度
bool   mEnhancedInternalEdgeRemoval;        // 内部边去除
float  mMass;                               // 质量 (kg)
float  mMaxStrength;                        // 最大推力 (N)
Vec3   mShapeOffset;                        // 形状偏移

// ExtendedUpdate 专用设置
struct ExtendedUpdateSettings {
    Vec3  mStickToFloorStepDown { 0, -0.5f, 0 };    // StickToFloor 下探距离
    Vec3  mWalkStairsStepUp { 0, 0.4f, 0 };         // 台阶上探高度
    float mWalkStairsMinStepForward { 0.02f };       // 最小前探距离
    float mWalkStairsStepForwardTest { 0.15f };      // 台阶下探前移距离
    float mWalkStairsCosAngleForwardContact { ... }; // 前进方向与接触法线最大角度(cos 75°)
    Vec3  mWalkStairsStepDownExtra { Vec3::sZero() };// 额外下探补偿
};
```

### 碰撞检测步骤

1. **收集碰撞**: `NarrowPhaseQuery::CastShape` 收集所有超过 padding 距离的接触
2. **碰撞归并**: 合并角度相近的碰撞 (余弦阈值控制)
3. **接触过滤**: 移除背面接触、无效应接触
4. **求解约束**: 同时满足所有接触平面约束
5. **滑动**: 沿约束面投影剩余速度

### 内部刚体 (Inner Rigid Body)

可选地，CharacterVirtual 可以有一个内部刚体：
- 使角色在物理世界中具有存在感（其他刚体可与之碰撞）
- 射线检测等可命中角色
- `LinearCast` 快速物体不能穿透

## 台阶检测 (Stair Walking)

通过 `ExtendedUpdate` 内部调用 `WalkStairs` 实现三步检测：

1. **Step Up**: 向上扫掠 `mWalkStairsStepUp` 距离
2. **Step Forward**: 沿前进方向扫掠 `mWalkStairsMinStepForward`
3. **Step Down**: 向下扫掠到新地面，额外加上 `mWalkStairsStepDownExtra`

台阶前先通过 `CanWalkStairs` 判断是否需要攀爬。台阶高度由 `mWalkStairsStepUp` 控制（默认 0.4m）。斜坡检测：表面法线与 up 方向夹角超过 `mMaxSlopeAngle` 则不可行走。

`ExtendedUpdate` 按顺序组合 `Update` → `StickToFloor` → `WalkStairs` 完成完整角色移动。

## 地面检测

- 通过 `GetGroundState()` 获取地面状态 (EGroundState 枚举)
- `OnGround` / `OnSteepGround` / `NotSupported` / `InAir`
- `IsSupported()` 在 OnGround 或 OnSteepGround 时返回 true
- 地面法线和速度通过 `GetGroundNormal()` / `GetGroundVelocity()` 获取

## 性能考虑

- CharacterVirtual 不参与 BroadPhase → 不需要每帧更新 AABB 树
- 碰撞检测是每帧按需执行的局部扫掠
- 可通过调整 `mCharacterPadding` 和 `mMaxCollisionIterations` 平衡精度与性能

## 注意事项

- CharacterVirtual 不能与所有形状完美交互（如超薄三角面）
- 当角色靠近尖锐边时可能出现不稳定的滑动
- 大量角色时建议使用 Character 而非全部使用 CharacterVirtual
