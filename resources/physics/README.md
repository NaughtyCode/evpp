# Physics Resources

JoltPhysics 引擎相关的数据和配置文件。

## 目录结构

```
resources/physics/
├── README.md           — 本文件
├── configs/            — 物理引擎配置文件
│   └── .gitkeep
└── data/               — 物理模拟数据
    └── .gitkeep
```

## configs/

存放物理引擎运行时配置。对应 JoltPhysics 的以下配置入口：

- `PhysicsSettings` — 全局模拟参数 (迭代次数、Baumgarte、睡眠阈值、确定性开关等)
- `BodyCreationSettings` — 刚体创建参数 (形状、运动类型、质量属性、CCD等)
- `SoftBodyCreationSettings` — 软体创建参数
- `VehicleConstraintSettings` — 车辆参数 (悬挂、车轮、引擎、防倾杆)
- `CharacterVirtualSettings` — 角色控制器参数

建议使用 JSON 格式存储，通过 glaze 库反序列化 (项目已在 `src/thirdparty/glaze` 中集成)。

### 示例 `simulation.json`

```json
{
  "numVelocitySteps": 10,
  "numPositionSteps": 2,
  "timeBeforeSleep": 0.5,
  "pointVelocitySleepThreshold": 0.03,
  "baumgarte": 0.2,
  "speculativeContactDistance": 0.02,
  "penetrationSlop": 0.02,
  "maxPenetrationDistance": 0.2,
  "deterministicSimulation": true,
  "constraintWarmStart": true,
  "gravity": [0.0, -9.81, 0.0]
}
```

### 示例 `body_defaults.json`

```json
{
  "motionType": "Dynamic",
  "motionQuality": "Discrete",
  "friction": 0.2,
  "restitution": 0.0,
  "linearDamping": 0.05,
  "angularDamping": 0.05,
  "maxLinearVelocity": 500.0,
  "maxAngularVelocity": 47.1,
  "gravityFactor": 1.0,
  "allowSleeping": true,
  "isSensor": false,
  "useManifoldReduction": true
}
```

## data/

存放物理模拟所需的数据文件：

- **碰撞网格**: 三角网格 (通过 `MeshShapeSettings` 加载顶点和索引)
- **高度场**: `HeightFieldShapeSettings` 高度采样数据
- **凸包数据**: `ConvexHullShapeSettings` 顶点列表
- **材质表**: `PhysicsMaterialSimple` 定义 (名称、颜色)
- **碰撞组配置**: `CollisionGroup` / `GroupFilterTable` 映射表
- **约束描述**: 关节 `ConstraintSettings` 子类定义和限制值

## 与 JoltPhysics 的集成点

### 1. 初始化
```cpp
// 注册分配器
JPH::RegisterDefaultAllocator();

// 创建作业系统
JobSystemThreadPool jobSystem(cMaxPhysicsJobs, cMaxPhysicsBarriers);

// 初始化物理系统
PhysicsSystem system;
system.Init(maxBodies, numMutexes, maxBodyPairs,
            maxContactConstraints,
            broadPhaseLayerInterface,      // 自定义: ObjectLayer → BroadPhaseLayer
            objectVsBroadPhaseFilter,       // 自定义: 层碰撞判断
            objectLayerPairFilter);         // 自定义: 层对碰撞判断
system.SetGravity(Vec3(0, -9.81f, 0));
system.SetPhysicsSettings(settings);        // 从 JSON 加载
```

### 2. 形状加载
```cpp
// 方式A: 通过 ShapeSettings (cooked — 可序列化)
BoxShapeSettings shapeSettings(Vec3(1, 1, 1));
ShapeRefC shape = shapeSettings.Create().Get();

// 方式B: 直接构造运行时 Shape (不能序列化)
ShapeRefC shape = new BoxShape(Vec3(1, 1, 1));
```

### 3. 材质管理
```cpp
// 简单刚体的摩擦/弹性在 Body 层面设置
BodyCreationSettings bodySettings;
bodySettings.mFriction = 0.3f;
bodySettings.mRestitution = 0.1f;

// 网格/高度场形状支持按子形状指定材质
PhysicsMaterialSimple *concrete = new PhysicsMaterialSimple("concrete", Color::sGrey);
PhysicsMaterialRefC matRef = concrete; // RefConst 持有引用计数
MeshShapeSettings meshSettings;
meshSettings.mMaterials = { matRef };  // 材质列表
```

### 4. 碰撞层映射
```cpp
// BroadPhaseLayerInterface 实现 — 映射 ObjectLayer → BroadPhaseLayer
class MyBroadPhaseLayerInterface : public BroadPhaseLayerInterface {
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override;
};

// ObjectLayerPairFilter 实现 — 决定两层是否碰撞
class MyLayerFilter : public ObjectLayerPairFilter {
    bool ShouldCollide(ObjectLayer inLayer1, ObjectLayer inLayer2) const override;
};
```

## engine.json (项目级)

项目已有的引擎级配置在 `resources/config/engine.json` 中，可通过 `ConfigManager` 加载。此处 `resources/physics/configs/` 专门存放纯物理层面的补充配置和预设。
