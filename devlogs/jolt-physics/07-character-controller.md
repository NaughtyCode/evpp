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

```cpp
struct CharacterVirtualSettings {
    float  mPredictiveContactDistance = 0.1f; // 预判距离
    uint   mMaxCollisionIterations = 5;       // 碰撞迭代
    uint   mMaxConstraintIterations = 15;     // 约束迭代
    float  mMinTimeRemaining = 1e-4f;         // 剩余时间阈值
    float  mCollisionTolerance = 1e-3f;       // 碰撞容差
    float  mCharacterPadding = 0.02f;         // 安全边距
    uint   mMaxNumHits = 256;                 // 最大碰撞数
    float  mHitReductionCosMaxAngle = 0.999f; // 碰撞归并角度
    float  mPenetrationRecoverySpeed = 1.0f;  // 穿透恢复速度
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

## 台阶检测 (Step Detection)

```cpp
// CharacterBase
virtual void SetMaxSlopeAngle(float);
virtual void SetCharacterUp(Vec3);
```

- 检测前方障碍高度
- 自动攀爬低于 `mMaxStepHeight` 的台阶
- 陡坡检测: 表面法线与向上方向夹角超过 `mMaxSlopeAngle` 则不可行走

## 地面检测

- 通过 `GetGroundState()` 获取地面状态
- `OnGround` / `InAir` / `NotSupported`
- 地面法线和速度通过 `GetGroundNormal()` / `GetGroundVelocity()` 获取

## 性能考虑

- CharacterVirtual 不参与 BroadPhase → 不需要每帧更新 AABB 树
- 碰撞检测是每帧按需执行的局部扫掠
- 可通过调整 `mCharacterPadding` 和 `mMaxNumHits` 平衡精度与性能

## 注意事项

- CharacterVirtual 不能与所有形状完美交互（如超薄三角面）
- 当角色靠近尖锐边时可能出现不稳定的滑动
- 大量角色时建议使用 Character 而非全部使用 CharacterVirtual
