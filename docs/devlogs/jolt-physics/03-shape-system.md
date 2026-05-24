# Shape System 深度分析

## 形状类型体系

Jolt 采用两层类型枚举：分类 `EShapeType` + 子类型 `EShapeSubType`，支持用户扩展。

### EShapeType (大类)

```
Convex    — 凸体 (球、盒、胶囊、凸包...)
Compound  — 复合形状
Decorated — 装饰器形状 (变换包装)
Mesh      — 三角网格
HeightField — 高度场
SoftBody  — 软体
Plane     — 无限平面
Empty     — 空形状
User1-4   — 用户自定义
```

### EShapeSubType (具体形状)

| 子类型 | 类别 | 说明 |
|--------|------|------|
| Sphere | Convex | 球体 |
| Box | Convex | 盒体 |
| Triangle | Convex | 三角面 |
| Capsule | Convex | 胶囊体 |
| TaperedCapsule | Convex | 锥形胶囊 |
| Cylinder | Convex | 圆柱 |
| TaperedCylinder | Convex | 锥形圆柱 |
| ConvexHull | Convex | 凸包 |
| StaticCompound | Compound | 静态复合形状 |
| MutableCompound | Compound | 可变复合形状 |
| RotatedTranslated | Decorated | 旋转平移包装 |
| Scaled | Decorated | 缩放包装 |
| OffsetCenterOfMass | Decorated | 质心偏移 |
| Mesh | Mesh | 三角网格 |
| HeightField | HeightField | 高度场 |
| SoftBody | SoftBody | 软体形状 |
| Plane | Plane | 无限平面 |
| Empty | Empty | 空 (无碰撞) |

### 用户扩展

支持 16 种用户自定义：
- `User1-8`: 通用
- `UserConvex1-8`: 凸体 (可使用 GJK/EPA 碰撞检测)

## Shape 基类接口

```cpp
class Shape : public RefTarget<Shape>, public NonCopyable {
    // 基础属性
    EShapeType     GetType();
    EShapeSubType  GetSubType();
    AABox          GetLocalBounds();       // 局部空间包围盒
    float          GetInnerRadius();       // 最大内接球半径
    Vec3           GetCenterOfMass();      // 质心偏移

    // 质量属性
    MassProperties GetMassProperties();    // 质量、惯性张量

    // 碰撞检测
    bool  CastRay(...);                    // 射线检测
    void  CollidePoint(...);               // 点包含检测
    void  CollideSoftBodyVertices(...);    // 软体顶点碰撞

    // 材料
    const PhysicsMaterial* GetMaterial(SubShapeID&);

    // 表面信息
    Vec3  GetSurfaceNormal(SubShapeID&, Vec3);
    void  GetSupportingFace(...);          // 支持面顶点

    // 浮力
    void  GetSubmergedVolume(...);         // 淹没体积计算

    // 三角形遍历
    void  GetTrianglesStart/Next(...);     // 按区域获取三角面

    // 缩放支持
    bool  IsValidScale(Vec3);
    Vec3  MakeScaleValid(Vec3);
};
```

## 形状共享与引用计数

`Shape` 继承自 `RefTarget<Shape>`，使用侵入式引用计数：

```cpp
using ShapeRefC = RefConst<Shape>;
```

同一形状可以被多个 Body 共享，Copy-on-Write 模式下通过 `Ref<Shape>` 获取可变引用 (Debug 模式断言引用计数为1)。

## ShapeSettings / Create 模式

两阶段创建：
1. **ShapeSettings**: 可序列化的形状参数 (uncooked form)
2. **Shape::Create()**: 生成优化的运行时形状 (cooked form)

```cpp
BoxShapeSettings settings;
settings.mHalfExtent = Vec3(1, 1, 1);
ShapeRefC shape = settings.Create().Get();
```

## 装饰器模式 (Decorator Pattern)

三个装饰器形状，可嵌套组合：

### RotatedTranslatedShape
附加旋转和平移变换：
```cpp
RotatedTranslatedShapeSettings(Vec3 position, Quat rotation, Shape* child);
```

### ScaledShape
非均匀缩放（各向异性），有严格限制：
- 球体/胶囊/圆柱：XZ 均匀缩放
- 复合形状：子形状无剪切

### OffsetCenterOfMassShape
质心偏移，不影响碰撞几何，仅影响动力学。

## 复合形状

### StaticCompoundShape
不可变复合，适合静态场景几何。子形状可重叠（体积计算近似）。

### MutableCompoundShape
运行时增删子形状，适用于可破坏物体。

## 其他形状

### MeshShape
三角网格。支持多种编码格式 `TriangleCodec`。
- `TriangleCodecIndexed8BitPackSOA4Flags`: 8-bit 索引 + 4-flag SoA 打包

### HeightFieldShape
高度场地形。支持多种材质。

### PlaneShape
无限大平面。适合地板等。

### EmptyShape
无碰撞形状。用于传感器/触发器。

## 碰撞形状常量: cDefaultConvexRadius

默认凸体半径 `cDefaultConvexRadius = 0.05f` 用于平滑接触，所有凸体形状包含此"皮肤"厚度。
