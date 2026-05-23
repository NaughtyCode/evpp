# Math Layer 深度分析

## 向量系统

Jolt 使用双层精度架构处理大世界坐标精度问题。

### Vec3 (32-bit float)
用于局部空间运算: 速度、力、小范围位移、碰撞检测。

```cpp
class alignas(JPH_VECTOR_ALIGNMENT) Vec3 {
    // SSE: __m128, NEON: float32x4_t, 标量: float[4]
    Type mValue;
};
```

### DVec3 / RVec3 (64-bit double)
`RVec3` = Real Vec3，实际类型由 `JPH_DOUBLE_PRECISION` 决定。

- **Double 模式**: 世界位置用 64-bit double，保证大世界精度
- **Float 模式**: 全部用 float，适用于小场景

### DMat44 / RMat44
4×4 变换矩阵，仅旋转部分使用 32-bit float，平移使用 64-bit double (或 float)。

```cpp
class DMat44 {  // double precision translation
    Vec4  mCol[3];   // 3x3 rotation columns (float)
    DVec3 mCol3;     // translation column (double), 4th element assumed 1
};
```

## SIMD 优化层级

| 架构 | 指令集 | 向量宽度 |
|------|--------|---------|
| x86 | SSE 4.1 → AVX → AVX2 → AVX512 | 128→256→512 bit |
| ARM | NEON | 128 bit |
| RISC-V | RVV | 可变长度 |
| WASM | SIMD128 | 128 bit |

### Vec4 (128-bit)
```cpp
class alignas(JPH_VECTOR_ALIGNMENT) Vec4 {
    // SSE:   __m128
    // NEON:  float32x4_t
    // Scalar: float[4]
};
```

关键运算全内联 + SIMD：
- `sMin/sMax/sClamp` — 逐分量 min/max/clamp
- `sFusedMultiplyAdd` — FMA (乘加融合)
- `Dot`/`DotV`/`DotV4` — 点积 (标量/SIMD 复制/Vec4 输出)
- `sSelect` — 基于掩码的选择

### UVec4 (无符号整数向量)
用于掩码、比较、位运算。`sEquals/sLess/sGreater` 返回 `UVec4` 作为 SIMD 掩码。

### Vec8/Vec16 (256/512-bit AVX)
渐进式支持，优先使用 128-bit 避免 AVX 相关降频惩罚。

## 矩阵 Mat44

存储为 4 个列向量 `Vec4`，既是 4×4 又是 4×3 仿射变换：

```cpp
class Mat44 {
    Vec4 mCol[4]; // 列主序存储
    // mCol[0-2]  = 旋转/缩放 (3x3)
    // mCol[3]    = 平移
};
```

支持操作:
- 旋转 (X/Y/Z/任意轴)、四元数→矩阵
- 平移、缩放、外积
- 逆矩阵、转置、行列式
- 四元数左乘/右乘辅助矩阵

## 四元数 Quat

单位四元数表示旋转，与 `Vec4` 共用底层存储：

```cpp
class Quat : public Vec4 {
    // x, y, z = 虚部, w = 实部
};
```

## Float3 / Double3

紧凑 3-float/double 存储（12/24 字节），用于内存中密集存储场景。加载/存储通过 `sLoadFloat3Unsafe`/`sStoreFloat3` 进行。

## 半精度浮点 HalfFloat

`Jolt/Core/HalfFloat.h` 提供 float16 编解码，主要用于 AABB 树的紧凑节点存储 (AABBTree NodeCodecQuadTreeHalfFloat)。

## 数学工具

| 头文件 | 功能 |
|--------|------|
| `Real.h` | 单/双精度统一类型 |
| `Math.h` | 基础函数: sqrt, sin, cos, atan2 等 |
| `TrigLookup.h` | 8-bit 三角函数查找表 |
| `Swizzle.h` | SIMD 通道重排 |
| `UVec4.h` / `UVec8.h` | 整数向量 |
| `GaussianDistribution.h` | 高斯分布生成 |

## 关键设计决策

1. **128-bit 作为默认**: 避免 AVX512 降频，跨 ARM/x86 一致
2. **FMA 可控**: `JPH_CROSS_PLATFORM_DETERMINISTIC` 时禁用 FMA 保证结果一致
3. **渐进式 SIMD 升级**: AVX→AVX2→AVX512 层次编译，自动选择最优路径
4. **双精度仅用于大坐标**: 旋转仍用 float，内存和性能开销最小化
