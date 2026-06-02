# 物理资产系统实现文档

## 背景分析

JoltPhysics 没有单独的通用资产管理器，它把可持久化物理数据分散在几个核心类型中：

- `ShapeSettings` / `Shape`：描述碰撞形状。`ShapeSettings::Create()` 构建运行时 `Shape`，`Shape::SaveWithChildren()` 和 `Shape::sRestoreWithChildren()` 支持带子形状与材质的二进制保存恢复。
- `BodyCreationSettings`：描述刚体创建参数，包括 shape、transform、motion type、object layer、速度、阻尼、质量、传感器、DOF 等。
- `TwoBodyConstraintSettings`：描述约束设置。Jolt 的 `PhysicsScene::ConnectedConstraint` 用 body 索引绑定约束两端。
- `PhysicsScene`：Jolt 的完整场景容器，内部保存 bodies、constraints、soft bodies，并通过 `CreateBodies(PhysicsSystem*)` 批量实例化。
- `ObjectStream`：Jolt 的 RTTI 文本/二进制对象流，用于序列化 `PhysicsScene`、shape settings、ragdoll 等高层对象。
- `StateRecorder`：运行时状态保存恢复，用于模拟状态快照，不等价于资产场景。

本工程之前的 `physics_assets.cc` 用自定义 JSON 资产直接创建静态体、动态原型和静态体之间的约束。主要缺口是：

- shape、body、constraint、scene schema 和加载逻辑全部集中在一个文件里。
- 没有运行时层的完整场景抽象，动态物体只能后续通过 `spawn(protoId)` 创建。
- 加载场景时不会实例化场景内动态对象，也不会为这些对象建立 diff 初始快照。

## 模块拆分

`src/runtime/physics/physics_assets.h` 保留为兼容聚合头，原有包含路径不需要改。实际实现拆分为以下模块：

- `physics_asset_common.*`
  - 公共 JSON 基础类型：`JsonMaterial`、`JsonTransform`、`PhysicsSceneBodyRecord`、`AssetLoadResult`。
  - 公共解析和验证：向量、四元数、motion type、motion quality、DOF、object layer、资产相对路径。
- `physics_shape_assets.*`
  - `JsonShapeDef` 和 `PhysicsShapeAssetFactory`。
  - 支持 `box`、`sphere`、`capsule`、`cylinder`、`convex_hull`/`convexHull`、`mesh`、`height_field`/`heightField`、`plane`、compound。
  - `JsonShapeDef.material` 现在会绑定到简单 shape 的 Jolt material；mesh / height field 继续支持 `params.materials`。
- `physics_body_assets.*`
  - `PrototypeEntry`、`JsonStaticBody`、`JsonDynamicPrototype`、新增 `JsonDynamicBody`。
  - `PhysicsBodyAssetFactory` 负责从 JSON 构建 prototype、static body settings、dynamic body settings。
  - `CreateBodySettingsFromPrototype()` 抽成统一函数，场景动态体和运行时 `CreateBody(proto)` 使用同一套 Jolt `BodyCreationSettings` 映射。
- `physics_constraint_assets.*`
  - `JsonConstraint` 和 `PhysicsConstraintAssetFactory`。
  - 约束现在通过场景体 id 查找，能连接静态体、动态体，也支持 `world` / `fixed` / `__world__` 表示固定世界。
- `physics_scene_asset.*`
  - 新增 `PhysicsSceneAsset`，作为运行时层完整物理场景。
  - 负责解析完整 JSON 场景，再由 `AssetLoader::LoadScene()` 实例化进 Jolt `PhysicsSystem`。

## 场景 JSON schema

顶层字段：

```json
{
  "materials": [],
  "staticBodies": [],
  "dynamicPrototypes": [],
  "dynamicBodies": [],
  "constraints": []
}
```

`staticBodies` 保持原语义：

```json
{
  "id": "ground",
  "shape": {
    "type": "box",
    "params": { "halfExtent": [10.0, 0.5, 10.0] }
  },
  "material": { "friction": 0.8, "restitution": 0.0 },
  "transform": {
    "position": [0.0, -0.5, 0.0],
    "rotation": [0.0, 0.0, 0.0, 1.0]
  },
  "objectLayer": "static"
}
```

`dynamicPrototypes` 定义可复用动态体模板：

```json
{
  "protoId": "crate",
  "shape": {
    "type": "box",
    "params": { "halfExtent": [0.5, 0.5, 0.5] }
  },
  "mass": 1.0,
  "material": { "friction": 0.4, "restitution": 0.1 },
  "motionType": "dynamic",
  "motionQuality": "discrete",
  "linearDamping": 0.05,
  "angularDamping": 0.05,
  "gravityFactor": 1.0,
  "objectLayer": "dynamic",
  "allowedDofs": [0, 1, 2, 3, 4, 5],
  "isSensor": false,
  "allowSleeping": true,
  "maxLinearVelocity": 500.0,
  "maxAngularVelocity": 47.1
}
```

新增 `dynamicBodies` 用于场景内动态对象实例：

```json
{
  "id": "crate_001",
  "protoId": "crate",
  "transform": {
    "position": [0.0, 5.0, 0.0],
    "rotation": [0.0, 0.0, 0.0, 1.0]
  },
  "userData": 1001,
  "activate": true,
  "linearVelocity": [0.0, 0.0, 0.0],
  "angularVelocity": [0.0, 0.0, 0.0]
}
```

`constraints` 现在可以引用任意场景体 id：

```json
{
  "type": "fixed",
  "bodyA": "ground",
  "bodyB": "crate_001",
  "pivot": [0.0, 0.0, 0.0],
  "axis": [0.0, 1.0, 0.0]
}
```

## 加载流程

`AssetLoader::LoadScene()` 的流程：

1. `PhysicsSceneAsset::LoadFromFile()` 读取 JSON，解析为完整场景对象。
2. 合并外部 `MaterialTable` 和场景内 `materials`。
3. 构建并创建所有 `staticBodies`，批量加入 broad phase，记录 body id 到场景 lookup。
4. 构建所有 `dynamicPrototypes` 到 prototype pool。
5. 构建并创建所有 `dynamicBodies`，按实例 `activate` 加入 physics world，记录 body id、asset id、初始 transform 和速度。
6. 创建所有 `constraints`，通过场景 lookup 解析 body id。
7. 返回 `AssetLoadResult` 和 `PhysicsSceneBodyRecord` 列表。

失败处理：

- 创建 body 或 constraint 过程中失败，会移除已添加约束、移除并销毁已创建 body，清空本次加载器状态。
- scene body id 跨静态体和动态体统一去重。
- prototype id 单独去重。

## PhysicsWorld 接入

`PhysicsWorld::Initialize()` 现在：

- 复制 `AssetLoader::GetPrototypes()` 到 `prototype_pool_`。
- 遍历 `GetSceneBodyRecords()` 注册所有场景体到 `ObjectRegistry`。
- 对动态场景体初始化 `state_snapshots_`，因此加载完成后这些动态对象会参与 transform 收集和 diff 生成。
- 日志输出新增 dynamic body 数量。

`PhysicsWorld::CreateBody()` 现在复用 `PhysicsBodyAssetFactory::CreateBodySettingsFromPrototype()`，避免场景动态体和运行时 spawn 的 Jolt settings 映射分叉。

## 与 JoltPhysics 的对应关系

运行时 `PhysicsSceneAsset` 对齐 Jolt `PhysicsScene` 的核心模型：

- `staticBodies` / `dynamicBodies` 对应 Jolt `PhysicsScene::mBodies` 中的 `BodyCreationSettings`。
- `dynamicPrototypes` 是本工程对 Jolt body settings 的模板池扩展，用于运行时 spawn。
- `constraints` 对应 Jolt `PhysicsScene::mConstraints`，但本工程用稳定字符串 id 而不是 body 数组索引连接。
- 当前运行时场景仍使用 JSON schema，而不是直接使用 Jolt `ObjectStream`，因为 JSON 更适合配置、热修改和服务器资源管理。

暂未覆盖 Jolt `PhysicsScene` 的 soft body 场景资产；软体需要引入 `SoftBodyCreationSettings`、shared settings 优化和相应 schema，后续可在新的 body 模块下继续扩展。

## 兼容性

- 原 `physics_assets.h` 的 `AssetLoader`、`JsonShapeDef`、`PrototypeEntry`、`AssetLoadResult` 仍可通过同一路径包含。
- 原 `staticBodies`、`dynamicPrototypes`、`constraints` JSON 字段继续可用。
- 新增字段是 `dynamicBodies`，旧场景文件不包含该字段时加载行为与之前一致。

## 验证

已执行：

- `cmake --preset windows-msvc -S src`
- `cmake --build artifacts/build --target GameServer --config Debug`

当前配置下没有生成 `integration.physics` 测试目标；`GameServer` Debug 构建已覆盖 physics runtime 源文件编译。
