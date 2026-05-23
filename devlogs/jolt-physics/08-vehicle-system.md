# Vehicle System 深度分析

## 车辆模型

基于 Marco Monster 的 "Car Physics for Games" 论文。

### VehicleConstraint 架构

```
VehicleConstraint (继承 Constraint + PhysicsStepListener)
  ├── VehicleAntiRollBars[]      // 防倾杆
  ├── Wheel[]                    // 车轮
  │    ├── WheelSettings          // 车轮配置
  │    └── Wheel                  // 运行时状态
  └── VehicleController           // 控制器 (输入/逻辑)
       ├── VehicleControllerKeyboard
       └── VehicleControllerTank  // 坦克式
```

### 悬挂系统

每个车轮通过悬挂弹簧连接到车体：
```cpp
struct WheelSettings {
    Vec3  mPosition;              // 悬挂连接点 (车体局部空间)
    Vec3  mSuspensionForcePoint;  // 轮胎力作用点 (车体局部空间)
    Vec3  mSuspensionDirection;   // 悬挂运动方向 (指向下方)
    Vec3  mSteeringAxis;          // 转向轴 (指向上方)
    Vec3  mWheelUp;               // 车轮上方向 (中性转向位置)
    Vec3  mWheelForward;          // 车轮前方向 (可设前束角)
    float mSuspensionMinLength;   // 最小压缩长度
    float mSuspensionMaxLength;   // 最大伸展长度
    float mSuspensionPreloadLength;// 预压长度
    SpringSettings mSuspensionSpring; // 弹簧 (频率+阻尼) 设置
    float mRadius;                // 车轮半径
    float mWidth;                 // 车轮宽度
    bool  mEnableSuspensionForcePoint;// 启用固定力作用点
};
```

弹簧力计算 (通过 `SpringSettings` 配置):
```
SpringSettings { ESpringMode::FrequencyAndDamping, frequency=1.5 Hz, damping=0.5 }
→ k = (2π * frequency)² * mass  (从频率推导刚度)
→ c = 2 * damping * √(k * mass)  (从阻尼比推导阻尼系数)
→ F = -k * displacement - c * velocity
```

### 轮胎摩擦模型

纵向 + 侧向摩擦力分开计算：

```
Longitudinal Slip:
  slip = (wheel_omega * radius - v_longitudinal) / |v_longitudinal|

Lateral Slip:
  slip_angle = atan2(v_lateral, |v_longitudinal|)

Friction Force = Load * FrictionCurve(slip)
```

摩擦曲线可通过 `WheelSettingsWV::mLongitudinalFriction` / `mLateralFriction` (类型 `LinearCurve`) 自定义。

### VehicleController (接口)

```cpp
class VehicleController : public NonCopyable {
public:
    explicit VehicleController(VehicleConstraint&);
    virtual ~VehicleController() = default;

protected:
    friend class VehicleConstraint;

    // 工厂: 从 WheelSettings 创建 Wheel 子类
    virtual Wheel*   ConstructWheel(const WheelSettings&) const = 0;

    // 是否允许车辆睡眠
    virtual bool     AllowSleep() const = 0;

    // StepListener 回调 (由 VehicleConstraint 转发)
    virtual void     PreCollide(float deltaTime, PhysicsSystem&) = 0;
    virtual void     PostCollide(float deltaTime, PhysicsSystem&) = 0;

    // 求解所有车轮的纵向/侧向约束
    virtual bool     SolveLongitudinalAndLateralConstraints(float deltaTime) = 0;

    // 状态保存/恢复
    virtual void     SaveState(StateRecorder&) const = 0;
    virtual void     RestoreState(StateRecorder&) = 0;

#ifdef JPH_DEBUG_RENDERER
    virtual void     Draw(DebugRenderer*) const = 0;
#endif

    VehicleConstraint& mConstraint;
};
```

具体子类 (如 `VehicleControllerKeyboard`) 在 `PreCollide`/`PostCollide` 中实现：
- 读取用户输入 (键盘/手柄)
- 计算引擎扭矩和转速
- 变速箱换挡逻辑
- 应用制动力

### 引擎/传动配置

```cpp
class VehicleControllerSettings : public RefTarget<VehicleControllerSettings> {
public:
    virtual VehicleController* ConstructController(VehicleConstraint&) const = 0;
};

动力系统分布在多个 Settings 中:
```cpp
// 引擎 (VehicleEngineSettings)
struct VehicleEngineSettings {
    float mMaxTorque;         // 最大扭矩 (Nm)
    float mMinRPM, mMaxRPM;   // 转速范围
    float mInertia;           // 转动惯量
    // ...
};

// 变速箱 (VehicleTransmissionSettings)
struct VehicleTransmissionSettings {
    float mClutchStrength;    // 离合器强度
    // ...
};

// 车轮专属 (WheelSettingsWV 子类, 非基类 WheelSettings)
struct WheelSettingsWV : WheelSettings {
    float mMaxBrakeTorque;       // 最大制动力矩
    float mMaxHandBrakeTorque;   // 手刹最大力矩
    LinearCurve mLongitudinalFriction; // 纵向摩擦曲线
    LinearCurve mLateralFriction;      // 侧向摩擦曲线
    float mInertia;              // 车轮转动惯量
    float mMaxSteerAngle;        // 最大转向角
};
```
```

### 防倾杆 (AntiRollBar)

连接左右轮的扭转弹簧：
```
左右悬挂压缩差 → 扭转力矩 → 抵消车身侧倾
```

```cpp
struct VehicleAntiRollBar {
    int   mLeftWheel;   // 左轮索引
    int   mRightWheel;  // 右轮索引
    float mStiffness;   // 刚度 (Nm/rad)
};
```

## 集成到主循环

`VehicleConstraint` 继承 `PhysicsStepListener`，必须注册到 `PhysicsSystem`：

1. **PreCollide** (StepListener): 更新车轮接地状态、应用悬挂力
2. **PostCollide** (StepListener): 更新车轮旋转、引擎转速、换挡

### 完整步骤
```
每个 Step:
1. Wheels::PreCollide    → 应用悬挂弹簧/阻尼力到车体
2. PhysicsSystem 碰撞检测 → 更新车轮接地状态
3. Wheels::PostCollide   → 更新摩擦、引擎、制动
4. 约束求解              → 纵向+侧向摩擦力作为约束
5. 积分                  → 更新车体位置/旋转
```

## 注意事项

- 车辆车轮与轻物体交互时可能出现下沉 (迭代求解器的已知限制)
- 可通过增加 solver 迭代次数或提高车轮接触点数量改善
- 使用 `mEnhancedInternalEdgeRemoval` 减少地形网格内部边问题
- `mMaxPitchRollAngle` 可限制车辆翻转角度
