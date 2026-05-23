# Memory & Allocation System 深度分析

## 内存分配架构

### 全局分配函数指针

Jolt 使用全局函数指针而非注册/切换模式：

```cpp
// 直接赋值覆盖即可替换
using AllocateFunction = void* (*)(size_t);
using ReallocateFunction = void* (*)(void*, size_t, size_t);
using FreeFunction = void (*)(void*);
using AlignedAllocateFunction = void* (*)(size_t, size_t);
using AlignedFreeFunction = void (*)(void*);

extern AllocateFunction Allocate;
extern ReallocateFunction Reallocate;
extern FreeFunction Free;
extern AlignedAllocateFunction AlignedAllocate;
extern AlignedFreeFunction AlignedFree;
```

### 初始化
```cpp
void RegisterDefaultAllocator();  // 使用 malloc/free 等平台默认实现
```

用户自定义示例：
```cpp
JPH::Allocate = [](size_t size) -> void* { return my_alloc(size); };
JPH::Free = [](void* ptr) { my_free(ptr); };
JPH::AlignedAllocate = [](size_t size, size_t align) -> void* {
    return my_aligned_alloc(size, align);
};
JPH::AlignedFree = [](void* ptr) { my_aligned_free(ptr); };
```

### 编译选项

- `JPH_DISABLE_CUSTOM_ALLOCATOR`: 禁用全局重载 `operator new`，改用直接函数调用
- 默认情况下：`JPH_OVERRIDE_NEW_DELETE` 宏在需要自定义分配器的类中重载 `operator new/delete`
- MinGW 32-bit 特殊情况：默认 new 可能只返回 8 字节对齐 → 强制使用 16 字节对齐分配

### 分配器适配器

| 类型 | 用途 |
|------|------|
| Allocate/Free | 通用内存 (默认 ≥ 16 字节对齐) |
| AlignedAllocate/AlignedFree | 显式对齐 (用于缓存行对齐) |
| TempAllocator | 帧临时的栈式/线性分配 |
| STLAllocator | STL 容器适配 `Allocate/Free` |
| STLAlignedAllocator | 对齐 STL 容器 |
| STLTempAllocator | STL 临时容器 |
| STLLocalAllocator | 每线程本地 STL 容器 |

### 对齐要求
```cpp
#define JPH_VECTOR_ALIGNMENT  16   // SSE/NEON: 16 字节
#define JPH_DVECTOR_ALIGNMENT 32   // AVX: 32 字节 (部分平台为 8)
#define JPH_CACHE_LINE_SIZE   64   // 缓存行
#define JPH_DEFAULT_ALLOCATE_ALIGNMENT __STDCPP_DEFAULT_NEW_ALIGNMENT__
```

`JPH_DVECTOR_ALIGNMENT` 并非所有平台都是 32：
- x86-64/x86-32/ARM-64/WASM/E2K: 32
- RISC-V 64-bit: 32
- RISC-V 32-bit/PowerPC/LoongArch: 8
- ARM 32-bit: 8

## FixedSizeFreeList / FixedSizeFreeListPolicy

定长对象池，用于 JobSystem 的 Job 和 Barrier 分配：
```cpp
template <typename T, typename Policy = ...>
class FixedSizeFreeList {
    // O(1) 分配/释放
    // 预分配连续内存
    // 使用原子操作维护空闲链表
};
```

## Body 内存布局

### Body 对象
```cpp
class Body {
    // 缓存行 0 (热): 变换数据
    RVec3 mPosition;          // 双精度位置 (或 float)
    Quat  mRotation;          // 旋转四元数
    // padding 到缓存行边界

    // 缓存行 1: 碰撞相关
    RefConst<Shape> mShape;   // 共享形状引用
    CollisionGroup  mCollisionGroup;
    ObjectLayer     mObjectLayer;

    // 缓存行 2: 标志位、接触缓存
};
```

### MotionProperties 缓存布局
```
Cache Line 0 [64B]: mLinearVelocity(16B) + mAngularVelocity(16B)
                    + mInvInertiaDiagonal(16B) + mInertiaRotation(16B)

Cache Line 1 [64B]: mForce(12B) + mTorque(12B) + mInvMass(4B)
                    + mLinearDamping(4B) + mAngularDamping(4B)
                    + mMaxLinearVelocity(4B) + mMaxAngularVelocity(4B)
                    + mGravityFactor(4B) + mIndexInActiveBodies(4B)
                    + mIslandIndex(4B)
                    + mMotionQuality(1B) + mAllowSleeping(1B)
                    + mAllowedDOFs(1B) + mNum*StepsOverride(2B)

Cache Line 2 [64B]: mSleepTestOffset(24B 双精度)/Spheres(48B)/Timer(4B)
                    + SimulationStats (条件编译)
```

## 序列化系统

### ObjectStream

双层流系统：

| 类型 | 格式 | 用途 |
|------|------|------|
| ObjectStreamText | 文本 | 人类可读，资产调试 |
| ObjectStreamBinary | 二进制 | 快速 I/O，存档 |

使用 RTTI 宏标记可序列化类型：
```cpp
JPH_DECLARE_SERIALIZABLE_VIRTUAL(..., ConstraintSettings)
JPH_DECLARE_SERIALIZABLE_ABSTRACT(..., ShapeSettings)
JPH_DECLARE_SERIALIZABLE_NON_VIRTUAL(..., BodyCreationSettings)
```

### Shape 序列化
```cpp
// 分步保存 (外部管理材质/子形状 ID 映射)
shape->SaveBinaryState(stream);
shape->SaveMaterialState(materialList);
shape->SaveSubShapeState(shapeList);

// 分步加载
Shape::sRestoreFromBinaryState(stream);
shape->RestoreMaterialState(materials, count);
shape->RestoreSubShapeState(shapes, count);

// 一步保存/恢复 (自包含，包含材质和子形状)
shape->SaveWithChildren(stream, shapeMap, materialMap);
Shape::sRestoreWithChildren(stream, shapeMap, materialMap);
```

### Body 状态快照
```cpp
// PhysicsSystem 级别
physicsSystem.SaveState(stateRecorder);
physicsSystem.RestoreState(stateRecorder);

// 单个 Body
physicsSystem.SaveBodyState(body, stateRecorder);
physicsSystem.RestoreBodyState(body, stateRecorder);
```

用于网络同步、确定性回滚、存档。

## 引用计数 (Reference)

侵入式引用计数，继承自 `RefTarget`：

```cpp
class Shape : public RefTarget<Shape> { ... };

using ShapeRefC = RefConst<Shape>;  // const 引用
using ShapeRef  = Ref<Shape>;       // 可变引用

// RefConst 任何情况下都可安全获取(只读)
RefConst<Shape> shapeRef = someShape;

// Ref 仅在引用计数 == 1 时可安全获取(Debug 模式断言检查)
// 实现 Copy-on-Write 模式
Ref<Shape> mutableRef = someShape;  // Debug: assert(refcount == 1)
```

## 工厂 (Factory)

`Jolt/Core/Factory.h` — 基于静态注册的类型工厂：
```cpp
class Factory {
    template<typename T> static bool Register();
    static void* CreateObject(const char* name);
    static const char* GetName(void* object); // 通过 RTTI
};
```

Shape 和 Constraint 的序列化反序列化依赖 Factory 按类型名创建实例。

## Shape 统计

### Stats 接口
```cpp
struct Shape::Stats {
    size_t mSizeBytes;      // 内存占用
    uint   mNumTriangles;   // 三角形数
};
// GetStats() — 仅本形状
// GetStatsRecursive(visited) — 含子形状 (去重)
```

### 典型内存占用

| 形状 | 近似 | 备注 |
|------|------|------|
| SphereShape / BoxShape | ~48 B | 基础几何 |
| CapsuleShape / CylinderShape | ~64 B | 高度 + 半径 |
| ConvexHullShape | 可变 | O(n) 顶点 |
| MeshShape | 可变 | O(n) 三角面 + BVH |
| HeightFieldShape | 可变 | O(w×h) |
