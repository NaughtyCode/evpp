# Collision Detection 深度分析

Jolt 使用经典的 Broad Phase + Narrow Phase 两阶段检测架构。

## Broad Phase (粗检测)

快速过滤出可能碰撞的刚体对，输出为 `BodyPair` 列表。

### 实现: BroadPhaseQuadTree

基于松散四叉树 (loose quad tree) 的空间划分结构。也支持暴力实现用于测试：

```
BroadPhase : BroadPhaseQuery (基类, 继承查询接口)
  ├── BroadPhaseBruteForce   (O(n²) 参考实现/单元测试)
  └── BroadPhaseQuadTree     (生产实现)
```

`BroadPhase::Init()` 接收 `BodyManager*` 和 `BroadPhaseLayerInterface&`。

关键接口：
```cpp
class BroadPhase : public BroadPhaseQuery {
    virtual void        Init(BodyManager*, const BroadPhaseLayerInterface&);
    virtual void        Optimize();          // 建树优化
    virtual UpdateState UpdatePrepare();     // 后台线程安全
    virtual void        UpdateFinalize(const UpdateState&);
    // 批量添加/移除刚体
    AddState  AddBodiesPrepare(BodyID*, int);
    void      AddBodiesFinalize(BodyID*, int, AddState, ...);
    void      RemoveBodies(BodyID*, int);
};
```

### BroadPhaseQuery (查询接口)
```cpp
class BroadPhaseQuery {
    // AABB 扫掠查询
    void CastAABox(const AABoxCast&, CastShapeBodyCollector&, ...);
    // 射线查询
    void CastRay(const RayCast&, RayCastBodyCollector&, ...);
    // AABB 相交查询
    void CollideAABox(const AABox&, CollideShapeBodyCollector&, ...);
    // 球体相交查询
    void CollideSphere(Vec3Arg inCenter, float inRadius, CollideShapeBodyCollector&, ...);
    // 点相交查询
    void CollidePoint(Vec3Arg, CollideShapeBodyCollector&, ...);
    // OBB 相交查询
    void CollideOrientedBox(const OrientedBox&, CollideShapeBodyCollector&, ...);
    // 获取整个 broadphase 的包围盒
    AABox GetBounds() const;
};
```

### 四叉树节点压缩
`AABBTree/NodeCodec/NodeCodecQuadTreeHalfFloat.h`：使用 half-float 压缩 AABB 节点，大幅减少内存。

## Narrow Phase (窄检测)

对每个 `BodyPair` 执行精确的形状-形状碰撞检测。

### 碰撞分派
`CollisionDispatch::sCollideShapeVsShape()` 根据形状类型查表选择碰撞函数：

| 组合 | 算法 |
|------|------|
| Convex × Convex | GJK + EPA |
| Convex × Mesh | 凸体 vs 三角遍历 |
| Convex × HeightField | 凸体 vs 高度场遍历 |
| Sphere × Mesh/HeightField | 球 vs 三角遍历 (特殊优化) |
| Compound × * | 拆解为子形状碰撞 |

### GJK/EPA (凸体检测)

- **GJK** (Gilbert–Johnson–Keerthi): 判断凸体相交，计算最近距离
- **EPA** (Expanding Polytope Algorithm): 当 GJK 检测到相交后，计算穿透深度和接触点

核心文件：
- `Jolt/Physics/Collision/GJK.*`
- `Jolt/Physics/Collision/EPA.*`
- `Jolt/Physics/Collision/EPAPenetration.*`

### Convex vs Triangles
- `CollideConvexVsTriangles.cpp`: 凸体与三角网格碰撞
- `CastConvexVsTriangles.cpp`: 凸体扫掠 (shape cast) 与三角网格
- `CollideSphereVsTriangles.cpp`: 球体专用快速路径

### 接触流形 (Contact Manifold)
`ManifoldBetweenTwoFaces.cpp`：当两个三角面碰撞时，计算面-面接触流形 (最多 4 个接触点)。

## 碰撞过滤

### 三层过滤
```
ObjectLayer → BroadPhaseLayer → BroadPhaseLayerPairFilter
                                 ↘ ObjectLayerPairFilter
```

1. **BroadPhaseLayerInterface**: 将用户自定义的 ObjectLayer 映射到内部 BroadPhaseLayer
2. **ObjectVsBroadPhaseLayerFilter**: 判断某 ObjectLayer 是否能与某 BroadPhaseLayer 碰撞
3. **ObjectLayerPairFilter**: 判断两 ObjectLayer 是否能碰撞

### CollisionGroup
更细粒度的碰撞控制：
```cpp
class CollisionGroup {
    CollisionGroup::GroupID mGroupID;       // 所属组
    CollisionGroup::SubGroupID mSubGroupID; // 子组掩码
};
```

### GroupFilter
自定义碰撞过滤回调，`GroupFilterTable` 是默认的表驱动实现。

## Shape Filter

在模拟期间可使用 `SimShapeFilter` 在单个 Body 内部排除某些子形状的碰撞。例如，高模和低模碰撞体同时存在时，在模拟中使用低模，射线检测使用高模。

## 连续碰撞检测 (CCD)

`EMotionQuality::LinearCast`: 对快速移动的物体，沿其运动轨迹进行扫掠检测。

```cpp
// PhysicsUpdateContext 中的 CCD 流程
JobFindCCDContacts()    // 找出需要 CCD 的刚体对
JobResolveCCDContacts() // 解析 CCD 接触并回退位置
```

## 碰撞统计

`NarrowPhaseStats.h` 提供全面的窄碰撞阶段性能统计，按形状对类型分类计数。

## 性能要点

- BroadPhase 使用松散四叉树，支持增量更新
- 批量添加刚体时构建临时子树，避免逐体更新
- 窄碰撞使用 `CollisionDispatch` 表驱动分派，O(1) 查找正确碰撞函数
- 球体碰撞有专门的快速代码路径
- 接触缓存: 连续帧之间缓存接触对，减少重复计算
