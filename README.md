# CullingEngine

CullingEngine 是一个 C++20 软件遮挡剔除库，提供 C 风格 ABI 入口，适合嵌入渲染引擎或工具链中做 CPU 侧 occlusion culling。项目以 CMake 构建，默认生成静态库，也可以切换为动态库。

当前项目版本：`1.6.0`

## 功能特性

- 基于软件光栅化的遮挡体渲染与 occludee AABB 批量可见性查询。
- 支持原始三角网格遮挡体提交，也支持预烘焙的紧凑遮挡体数据。
- 支持 coherent render mode、上一帧深度/遮挡体复用、遮挡体优先队列等性能配置。
- 提供深度图、帧捕获、日志回调和调试图像输出能力。
- 公开 C ABI，便于从游戏引擎、插件或其他语言绑定中调用。
- CMake 原生支持 Windows、Linux、macOS、iOS、Android 平台检测。

## 目录结构

```text
.
├── cmake/                  # CMake package config 模板
├── include/                # 公共头文件与 C API
├── src/                    # 引擎实现
├── tests/                  # 单元测试与 replay/smoke 测试
├── thirdparty/             # doctest、googletest 等第三方依赖
└── CMakeLists.txt          # 顶层构建脚本
```

## 环境要求

- CMake `3.16` 或更高版本
- 支持 C++20 的编译器：
  - MSVC
  - Clang
  - GCC
- 目标平台需支持项目使用的 SIMD 路径：
  - x86/x64 native 平台使用 SSE 相关 intrinsic
  - Android / ARM64 平台使用 ARM/NEON 兼容路径

## 快速构建

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

默认会构建 `CullingEngine` 静态库并启用测试。

### 常用构建选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CullingEngine_BUILD_SHARED` | `OFF` | 构建动态库而不是静态库 |
| `CullingEngine_BUILD_TESTS` | `ON` | 构建测试目标 |
| `CullingEngine_NATIVE_DEBUG` | `ON` | 启用 native debug/replay 相关功能 |

示例：

```powershell
cmake -S . -B build-shared `
  -DCMAKE_BUILD_TYPE=Release `
  -DCullingEngine_BUILD_SHARED=ON `
  -DCullingEngine_BUILD_TESTS=ON

cmake --build build-shared --config Release
```

## 运行测试

```powershell
ctest --test-dir build --output-on-failure -C Release
```

项目当前配置了两个测试：

- `CullingEngine_unit_tests`：doctest 单元测试，覆盖 API、mesh baking、日志/捕获、数学工具和遮挡工作流。
- `CullingEngine_replay_smoke`：replay 程序的 smoke 测试，不依赖外部 golden capture 数据。

也可以直接运行测试可执行文件：

```powershell
.\build\tests\Release\CullingEngine_unit_tests.exe
.\build\tests\Release\CullingEngine_replay_test.exe --smoke
```

如果使用单配置生成器，路径通常是：

```powershell
.\build\tests\CullingEngine_unit_tests
.\build\tests\CullingEngine_replay_test --smoke
```

## 安装与集成

安装：

```powershell
cmake --install build --prefix install --config Release
```

在其他 CMake 项目中使用：

```cmake
find_package(CullingEngine CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE CullingEngine::CullingEngine)
```

如果不安装，也可以把本项目作为子目录：

```cmake
add_subdirectory(path/to/CullingEngine)
target_link_libraries(MyApp PRIVATE CullingEngine)
```

## 最小使用示例

```cpp
#include "CullingEngineAPI.h"

int main()
{
    // width 必须不小于 64、且能被 64 整除；
    // height 必须不小于 8、且能被 8 整除。
    void* engine = CullingEngineInit(1024, 512, 1.0f);
    if (engine == nullptr)
    {
        return 1;
    }

    float cameraPos[3] = {0.0f, 0.0f, 0.0f};
    float cameraDir[3] = {0.0f, 0.0f, 1.0f};
    float viewProj[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    CullingEngineStartNewFrame(engine, cameraPos, cameraDir, viewProj, false);

    float localToWorld[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    const float occluderVertices[] = {
        -1.0f, -1.0f, 1.0f,
         1.0f, -1.0f, 1.0f,
         1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f, 1.0f,
    };
    const unsigned short occluderIndices[] = {
        0, 1, 2,
        0, 2, 3,
    };

    CullingEngineRenderOccluder(
        engine,
        occluderVertices,
        occluderIndices,
        4,
        6,
        localToWorld,
        false,
        true);

    CullingEngineSet(engine, CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER, 0);

    float boxes[] = {
        -0.25f, -0.25f, 1.0f,
         0.25f,  0.25f, 2.0f,
    };
    bool visible[1] = {};

    CullingEngineQueryOccludees(engine, boxes, 1, visible);

    CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 1);
    return visible[0] ? 0 : 2;
}
```

## Mesh Baking

对重复使用的遮挡体，可以先将原始网格烘焙为紧凑数据，然后在帧内提交烘焙结果：

```cpp
void* bakeBuffer = CullingEngineCreateOccluderBakeBuffer();

int compressedSize = 0;
unsigned short* baked = CullingEngineMeshBake(
    bakeBuffer,
    &compressedSize,
    vertices,
    indices,
    vertexCount,
    indexCount,
    15.0f,
    true,
    true,
    0);

if (baked != nullptr)
{
    int rasterizedTriangles = 0;
    CullingEngineRenderBakedOccluder(
        engine,
        baked,
        localToWorld,
        false,
        &rasterizedTriangles);
}

CullingEngineDestroyOccluderBakeBuffer(bakeBuffer);
```

`baked` 指向 bake buffer 内部存储；如果需要长期保存，请在销毁或复用 bake buffer 前复制输出数据。

## 常用 API 流程

1. 调用 `CullingEngineInit` 创建引擎实例。
2. 每帧调用 `CullingEngineStartNewFrame` 设置相机位置、方向和 view-projection 矩阵。
3. 调用 `CullingEngineRenderOccluder` 或 `CullingEngineRenderBakedOccluder` 提交遮挡体。
4. 必要时调用 `CullingEngineSet(..., CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER, 0)` 强制刷新已提交遮挡体。
5. 调用 `CullingEngineQueryOccludees` 批量查询 AABB 可见性。
6. 退出时调用 `CullingEngineSet(..., CULLING_ENGINE_DESTROY, 1)` 销毁实例。

更多配置 ID 和同步命令见 `include/CullingEngineMacros.h` 与 `include/CullingEngineAPI.h`。

## 调试与捕获

项目提供以下调试相关能力：

- `CullingEngineSetLogFunc`：设置进程级日志回调。
- `CULLING_ENGINE_GET_DEPTH_MAP` / `CULLING_ENGINE_SAVE_DEPTH_MAP`：读取或保存深度图。
- `CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH`：设置帧捕获输出目录。
- `CULLING_ENGINE_CAPTURE_FRAME`：捕获下一帧。
- `CullingEngineGetLatestCaptureDepthMapFilename` / `CullingEngineGetLatestCapFilename`：读取最近一次捕获输出路径。

native debug/replay 功能由 `CullingEngine_NATIVE_DEBUG` 控制。

## 第三方依赖

- `thirdparty/doctest`：当前单元测试使用的轻量测试框架。
- `thirdparty/googletest`：随仓库保留的 GoogleTest/GoogleMock 源码。

## 备注

- `build/`、`build-shared/` 和 `artifacts/` 目录已在 `.gitignore` 中忽略。
- 根 API 入口集中在 `include/CullingEngineAPI.h`，该文件包含参数约束、生命周期和配置命令说明。
