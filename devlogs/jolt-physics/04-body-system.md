# Body System 深度分析

## 刚体层级

```
BodyManager
  └── Body[]
       ├── Shape (碰撞几何, 引用计数共享)
       ├── MotionProperties (运动状态, 仅 Dynamic/Kinematic)
       │    ├── 线/角速度
       │    ├── 质量/惯性张量
       │    ├── 阻尼 + 重力因子
       │    ├── 睡眠检测 (3点法)
       │    └── 速度/迭代覆盖
       └── BodyCreationSettings → (创建参数, 支持序列化)
```

## Body (刚体)

### 运动类型 (EMotionType)

| 类型 | 描述 | 受物理影响 | 有 MotionProperties |
|------|------|-----------|---------------------|
| Static | 静态不可动 | 否 | 否(除非 mAllowDynamicOrKinematic) |
| Kinematic | 运动学控制 | 否 (手动设速) | 是 |
| Dynamic | 完全物理模拟 | 是 | 是 |

### BodyType (刚体种类)
- **Rigid**: 标准刚体
- **Soft**: 软体

### 关键属性
```cpp
class alignas(max(JPH_VECTOR_ALIGNMENT, JPH_RVECTOR_ALIGNMENT)) Body {
    // 变换 (COM 位置 + 四元数旋转)
    RVec3 mPosition;             // 世界空间质心位置
    Quat  mRotation;             // 世界空间质心旋转
    AABox mBounds;               // 世界空间包围盒 (缓存)

    // 形状引用 (共享, 侵入式引用计数)
    RefConst<Shape> mShape;

    // 运动属性 (仅 Dynamic/Kinematic 有值)
    MotionProperties* mMotionProperties;

    // 碰撞属性
    ObjectLayer    mObjectLayer;
    BroadPhaseLayer mBroadPhaseLayer; // 由 ObjectLayer 映射
    CollisionGroup mCollisionGroup;

    // 物理材料
    float mFriction;             // 默认 0.2
    float mRestitution;          // 默认 0.0

    // 核心类型标识
    BodyID     mID;
    EBodyType  mBodyType;        // RigidBody / SoftBody
    EMotionType mMotionType;     // Static / Kinematic / Dynamic

    // 标志位 (原子操作, bit flags)
    //   IsSensor, CollideKinematicVsNonDynamic, UseManifoldReduction,
    //   ApplyGyroscopicForce, EnhancedInternalEdgeRemoval,
    //   IsInBroadPhase, InvalidateContactCache
    atomic<uint8> mFlags;
};
```

> **注意**: `mIsSensor`, `mUseManifoldReduction`, `mApplyGyroscopicForce` 等均通过 getter/setter 存取 `mFlags` 中的 bit flags, 而非独立 bool 成员. `mMotionQuality` 存储在 `MotionProperties` 中, 通过 `GetMotionProperties()->mMotionQuality` 访问. Body 的位置/旋转存储的是**质心 (COM)** 空间, 非形状原点.

## BodyManager

管理所有 Body 的中央注册表：

```cpp
class BodyManager {
    // 存储
    Body*   mBodies[...];       // 定长 Body 数组
    BodyID* mActiveBodies[2];   // 活跃列表 (Rigid + Soft)

    // 并行互斥锁 (细粒度分段)
    MutexArray mMutexes;        // body_id 取模定位锁

    // 接口
    Body* AllocateBody(const BodyID&);
    void  FreeBody(Body*);
    void  ActivateBody(Body*);
    void  DeactivateBody(Body*);
};
```

### 锁机制
- `BodyLockInterfaceLocking`: 带锁访问，安全多线程
- `BodyLockInterfaceNoLock`: 无锁访问，仅单线程或确定安全时使用
- `MutexArray` 基于 `body_id.GetIndex() & (numMutexes - 1)` 分段

## BodyInterface (用户 API)

所有对刚体的操作通过此接口进行：

```cpp
class BodyInterface {
    // 生命周期
    Body*  CreateBody(const BodyCreationSettings&);
    Body*  CreateBodyWithID(const BodyID&, ...);
    BodyID CreateAndAddBody(const BodyCreationSettings&, EActivation);
    void   DestroyBody(const BodyID&);
    void   AddBody(const BodyID&, EActivation);
    void   RemoveBody(const BodyID&);

    // 批量操作 (比逐体添加快数百倍)
    AddState AddBodiesPrepare(BodyID*, int);
    void     AddBodiesFinalize(BodyID*, int, AddState, EActivation);
    void     AddBodiesAbort(BodyID*, int, AddState);
    void     RemoveBodies(BodyID*, int);

    // 高级: 分离分配/注册 (网络同步用途)
    Body*  CreateBodyWithoutID(const BodyCreationSettings&);
    bool   AssignBodyID(Body*, const BodyID&);
    Body*  UnassignBodyID(const BodyID&);
    void   DestroyBodyWithoutID(Body*);

    // 变换
    void SetPositionAndRotation(BodyID, RVec3, Quat, EActivation);
    void SetPositionAndRotationWhenChanged(...); // 仅变动>ε时更新
    void SetLinearAndAngularVelocity(BodyID, Vec3, Vec3);
    void MoveKinematic(BodyID, RVec3 targetPos, Quat targetRot, float dt);

    // 力/冲量
    void AddForce(BodyID, Vec3);
    void AddForce(BodyID, Vec3, RVec3 point);
    void AddTorque(BodyID, Vec3);
    void AddImpulse(BodyID, Vec3);
    void AddImpulse(BodyID, Vec3, RVec3 point);
    void AddAngularImpulse(BodyID, Vec3);
    bool ApplyBuoyancyImpulse(BodyID, ...);

    // 激活/睡眠
    void ActivateBody(BodyID);
    void DeactivateBody(BodyID);
    bool IsActive(BodyID);
    void ResetSleepTimer(BodyID);
};
```

## MotionProperties

存储动态刚体的运动状态，设计为三个缓存行：

```
第1缓存行 (最热):
  Vec3  mLinearVelocity       // 线速度 (COM) 
  Vec3  mAngularVelocity      // 角速度
  Vec3  mInvInertiaDiagonal   // 逆惯性张量对角 D
  Quat  mInertiaRotation      // 惯性旋转 R, I_body⁻¹ = R·D·R⁻¹

第2缓存行 (热):
  Float3 mForce               // 累计力
  Float3 mTorque              // 累计扭矩
  float  mInvMass             // 逆质量
  float  mLinearDamping       // 默认 0.05
  float  mAngularDamping      // 默认 0.05
  float  mMaxLinearVelocity   // 默认 500 m/s
  float  mMaxAngularVelocity  // 默认 0.25*π*60 ≈ 47.1 rad/s
  float  mGravityFactor       // 默认 1.0
  uint32 mIndexInActiveBodies // 在活跃列表中的索引
  uint32 mIslandIndex         // 所属 Island 索引
  EMotionQuality mMotionQuality      // 运动质量 (Discrete / LinearCast)
  bool   mAllowSleeping              // 允许睡眠
  EAllowedDOFs mAllowedDOFs          // 自由度约束
  uint8  mNumVelocityStepsOverride   // 速度迭代覆盖 (0=全局默认)
  uint8  mNumPositionStepsOverride   // 位置迭代覆盖 (0=全局默认)

第3缓存行 (冷):
  Double3 mSleepTestOffset    // 睡眠检测参考偏移 (双精度模式)
  Sphere  mSleepTestSpheres[3]// 三点睡眠检测球
  float   mSleepTestTimer     // 静止计时器
```

### 自由度约束 (AllowedDOFs)
```cpp
enum class EAllowedDOFs : uint8 {
    All                  = 0b111111,
    TranslationX         = 0b000001,
    TranslationY         = 0b000010,
    TranslationZ         = 0b000100,
    RotationX            = 0b001000,
    RotationY            = 0b010000,
    RotationZ            = 0b100000,
    Plane2D              = 0b001011, // X+Z平移, Y旋转
};
```

通过 `LockTranslation()`/`LockAngular()` 掩码运算钳制被锁定分量。

### 睡眠系统

三点运动检测法：
1. 在 Body 上选取 3 个特征点:
   - COM (质心)
   - COM + 最高边界盒轴方向
   - COM + 次高边界盒轴方向
2. 记录每点位置球体
3. 若所有三点速度 < `mPointVelocitySleepThreshold` (默认 0.03 m/s)
   且持续时间 > `mTimeBeforeSleep` (默认 0.5s) → 进入睡眠
4. 睡眠刚体间互不碰撞检测 (节省大量计算)

## BodyCreationSettings

完整的创建参数：

```cpp
struct BodyCreationSettings {
    // 变换
    RVec3               mPosition = RVec3::sZero();
    Quat                mRotation = Quat::sIdentity();
    Vec3                mLinearVelocity = Vec3::sZero();
    Vec3                mAngularVelocity = Vec3::sZero();

    // 用户数据
    uint64              mUserData = 0;

    // 碰撞
    ObjectLayer         mObjectLayer = 0;
    CollisionGroup      mCollisionGroup;

    // 模拟属性
    EMotionType         mMotionType = EMotionType::Dynamic;
    EAllowedDOFs        mAllowedDOFs = EAllowedDOFs::All;
    bool                mAllowDynamicOrKinematic = false;   // 静态体也可后续切换
    bool                mIsSensor = false;
    bool                mCollideKinematicVsNonDynamic = false; // 运动学→静态碰撞
    bool                mUseManifoldReduction = true;
    bool                mApplyGyroscopicForce = false;       // 陀螺力
    EMotionQuality      mMotionQuality = EMotionQuality::Discrete;
    bool                mEnhancedInternalEdgeRemoval = false; // 网格内部边去除
    bool                mAllowSleeping = true;

    // 物理参数
    float               mFriction = 0.2f;
    float               mRestitution = 0.0f;
    float               mLinearDamping = 0.05f;
    float               mAngularDamping = 0.05f;
    float               mMaxLinearVelocity = 500.0f;          // m/s
    float               mMaxAngularVelocity = 0.25f * PI * 60.0f; // rad/s
    float               mGravityFactor = 1.0f;

    // 求解覆盖
    uint                mNumVelocityStepsOverride = 0;        // 0=使用全局设置
    uint                mNumPositionStepsOverride = 0;

    // 质量属性
    EOverrideMassProperties mOverrideMassProperties = CalculateMassAndInertia;
    float               mInertiaMultiplier = 1.0f;           // 惯性缩放
    MassProperties      mMassPropertiesOverride;              // 自定义质量属性

private:
    // 形状 (二选一, 互斥)
    RefConst<ShapeSettings>  mShape;     // 可序列化 (uncooked)
    RefConst<Shape>          mShapePtr;  // 运行时优化 (cooked)
};
```

`mOverrideMassProperties` 枚举：
- `CalculateMassAndInertia`: 从密度自动计算质量和惯性
- `CalculateInertia`: 使用给定质量，自动计算惯性
- `MassAndInertiaProvided`: 使用给定的质量和惯性
