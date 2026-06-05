# Perfetto 打桩机制深度分析与运行时开关方案

日期：2026-06-05

## 1. 结论摘要

本项目没有直接在业务代码中使用 `src/thirdparty/perfetto` 的底层 API，而是在
`src/runtime/profiler` 建了一层引擎封装：

- `profiler_categories.h` 负责声明 Perfetto TrackEvent categories。
- `profiler_core.cc/.h` 负责 Perfetto runtime 初始化、session 生命周期和 trace 文件读写。
- `profiler_macros.h` 提供基础打桩宏。
- `profiler_events.h` 提供帧、物理、脚本、实体、空间、AOI、认证等业务便捷宏。

Perfetto 原生 category 过滤发生在 trace session 配置阶段，适合“启动采集时选择类别”，
但不适合作为业务运行中的细粒度批量开关。因此新增的运行时开关机制放在
`ENGINE_PROFILE_*` 宏入口，而不是修改 `thirdparty/perfetto/perfetto.h/.cc`。

本次实现新增：

- `ProfilerEventGroup` 分组位图和运行时总开关。
- `ProfilerManager` 运行时控制 API。
- 分组感知的 `ENGINE_PROFILE_*_GROUP` 宏。
- 基础宏按 category 自动映射分组，高层便捷宏使用显式分组。
- scope begin/end 状态保持，避免运行中切换导致 trace slice 不闭合。
- profiler ON/OFF 两种编译路径的单测覆盖。

## 2. 当前打桩机制

### 2.1 第三方 Perfetto 集成层

第三方源码位于：

- `src/thirdparty/perfetto/perfetto.h`
- `src/thirdparty/perfetto/perfetto.cc`
- `src/thirdparty/perfetto/CMakeLists.txt`

`CMakeLists.txt` 将 Perfetto 编译为 `engine_perfetto_static`。项目侧在 server/tests
开启 `ENGINE_PROFILER_ENABLED` 时链接该静态库。

当前没有修改 Perfetto amalgamated 源码，这是正确方向：`perfetto.h/.cc` 体积大、升级成本高，
业务开关不应侵入第三方 SDK。

### 2.2 Category 声明与静态存储

`src/runtime/profiler/profiler_categories.h` 使用：

```cpp
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("engine"),
    perfetto::Category("engine.entity"),
    ...
);
```

当前声明了 14 个 TrackEvent category：

| Category | 含义 |
| --- | --- |
| `engine` | 引擎通用、生命周期、帧循环 |
| `engine.entity` | 实体生命周期和查询 |
| `engine.space` | Space 生命周期和消息路由 |
| `engine.aoi` | AOI / 空间查询 |
| `engine.auth` | 认证和 session |
| `engine.script` | 脚本系统 |
| `engine.physics` | 物理系统 |
| `engine.timer` | 定时器 |
| `engine.vm` | VM 操作 |
| `engine.net` | 网络编码和传输 |
| `engine.rpc` | RPC 调度 |
| `engine.db` | 数据库和 ORM |
| `engine.monitoring` | 指标、健康检查、管理 HTTP |
| `engine.config` | 配置加载与校验 |

`src/runtime/profiler/profiler_core.cc` 在全局作用域调用：

```cpp
PERFETTO_TRACK_EVENT_STATIC_STORAGE();
```

Perfetto 要求 category 声明和 static storage 位于同一 TrackEvent namespace。当前做法正确，
并且 static storage 只出现一次。

### 2.3 初始化与 session 生命周期

`ProfilerManager::Initialize()` 做两件关键工作：

1. `perfetto::Tracing::Initialize(args)`
2. `perfetto::TrackEvent::Register()`

`ProfilerManager::StartSession()` 构造 `perfetto::TraceConfig`，添加 `track_event` data source，
然后创建 in-process tracing session：

```cpp
session_ = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
session_->Setup(cfg);
session_->StartBlocking();
```

停止时通过：

```cpp
session_->StopBlocking();
cached_trace_ = session_->ReadTraceBlocking();
```

随后由 `SaveTrace()` / `SaveTraceExact()` 写入 `.perfetto-trace` 文件。

### 2.4 宏层打桩路径

业务代码主要包含 `runtime/profiler/profiler_events.h`，再由它包含基础宏。

基础宏原先直接展开到 Perfetto：

```cpp
ENGINE_PROFILE_SCOPE(cat, name, ...)   -> TRACE_EVENT(cat, name, ...)
ENGINE_PROFILE_BEGIN(cat, name, ...)   -> TRACE_EVENT_BEGIN(cat, name, ...)
ENGINE_PROFILE_END(cat)                -> TRACE_EVENT_END(cat)
ENGINE_PROFILE_INSTANT(cat, name, ...) -> TRACE_EVENT_INSTANT(cat, name, ...)
ENGINE_PROFILE_COUNTER(...)            -> TRACE_COUNTER(...)
```

高层宏再组合基础宏，例如：

```cpp
ENGINE_PROFILE_FRAME_BEGIN(frame_no, dt_ms)
ENGINE_PROFILE_PHYSICS_STEP(delta_time)
ENGINE_PROFILE_SCRIPT_DOFILE(path)
ENGINE_PROFILE_AOI_QUERY()
```

当前业务打桩分布：

- `src/runtime` 中 24 个非 profiler 源文件使用了 `ENGINE_PROFILE_*`。
- 非 profiler 目录约 124 处 `ENGINE_PROFILE_*` 调用。
- 直接 `ENGINE_PROFILE_SCOPE("...")` 的 category 分布集中在
  `engine.script`、`engine.physics`、`engine.entity`、`engine.vm` 等热路径。

## 3. Perfetto 原生过滤机制边界

Perfetto TrackEvent 宏展开后会：

1. 根据静态字符串 category 在编译期查找 category index。
2. 通过 category registry 中的原子状态判断该 category 当前是否被某个 tracing session 启用。
3. category 启用时才执行事件写入 lambda。

这个机制已经很高效，但它的控制源是 `TrackEventConfig`：

```cpp
track_event_config.add_enabled_categories(...)
track_event_config.add_disabled_categories(...)
```

也就是说，原生过滤主要适合 session setup 时确定采集类别。它不能很好地解决以下需求：

- 运行中临时关闭一批业务桩，不重启 trace session。
- 按业务语义分组控制，例如 `frame` 和 `engine` category 不完全等价。
- 对跨作用域 begin/end 事件保持一致开关状态。
- 在 profiler 宏入口就跳过 debug annotation 参数求值。

因此，本次新增的运行时开关放在引擎宏层。Perfetto 原生 category 过滤仍保留为第二道过滤。

## 4. 新增运行时开关设计

### 4.1 分组模型

新增 `src/runtime/profiler/profiler_switches.h/.cc`。

核心类型：

```cpp
using ProfilerEventGroupMask = uint64_t;

enum class ProfilerEventGroup : ProfilerEventGroupMask {
    Engine,
    Frame,
    Timer,
    Physics,
    Script,
    Entity,
    Space,
    Aoi,
    Auth,
    Vm,
    Network,
    Rpc,
    Database,
    Monitoring,
    Config,
    All
};
```

运行时状态是两个原子量：

- `g_profiler_runtime_enabled`：总开关。
- `g_profiler_enabled_groups`：分组启用位图。

热路径查询：

```cpp
ProfilerRuntimeIsGroupEnabled(group)
```

该函数只做 relaxed 原子读取和位运算，不加锁，不触碰 `ProfilerManager` mutex。

### 4.2 Category 到分组的自动映射

基础宏仍支持原用法：

```cpp
ENGINE_PROFILE_SCOPE("engine.physics", "Step")
```

宏会通过：

```cpp
ProfilerEventGroupFromCategory("engine.physics")
```

映射到 `ProfilerEventGroup::Physics`。这样直接使用基础宏的存量桩点不需要全部改写。

高层便捷宏使用显式分组，例如：

```cpp
ENGINE_PROFILE_SCOPE_GROUP(ProfilerEventGroup::Physics, "engine.physics", "PhysicsStep", ...)
```

这样 `frame` 这类业务批次可以独立于 `engine` category 控制。

### 4.3 Scope 事件的一致性

Perfetto 原生 `TRACE_EVENT` 是 RAII scope，begin 在构造时写入，end 在析构时写入。

如果简单写成：

```cpp
if (enabled) TRACE_EVENT(...)
```

会破坏 RAII 生命周期，scope 会在宏内部结束，无法覆盖调用者作用域。

本次实现使用自定义宏 finalizer：

```cpp
ENGINE_PROFILE_SCOPE_GROUP(group, cat, name, ...)
```

展开逻辑等价于：

1. 在调用者作用域定义一个局部 finalizer。
2. 构造时检查 group 开关。
3. 若启用，写入 `TRACE_EVENT_BEGIN`。
4. finalizer 析构时使用构造时保存的 `enabled` 状态决定是否写入 `TRACE_EVENT_END`。

因此即使运行中关闭该 group，已经开始的 scope 仍能正常闭合。

### 4.4 Manual begin/end 的一致性

`ENGINE_PROFILE_BEGIN/END` 这类跨调用事件无法靠单个局部 finalizer 保存状态。

新增做法：

- begin 时将本次是否写入 begin 压入线程本地栈。
- end 时弹栈；只有对应 begin 真正写入时才写 end。

线程本地栈：

```cpp
inline thread_local std::vector<unsigned char> g_profiler_manual_scope_stack;
```

这可以避免运行中切换 group 导致不匹配的 begin/end。

### 4.5 参数求值策略

运行时 group 关闭时，宏不会执行 Perfetto 宏体，因此 debug annotation 参数不会求值。

示例：

```cpp
int evaluated = 0;
ProfilerManager::Get().SetEventGroupEnabled(ProfilerEventGroup::Physics, false);
ENGINE_PROFILE_INSTANT("engine.physics", "X", "value", ++evaluated);
// evaluated 仍为 0
```

这对热路径很重要：关闭某批桩后，不只是 trace 不写入，额外参数构造成本也会被跳过。

## 5. 新 API

### 5.1 ProfilerConfig

`ProfilerConfig` 新增：

```cpp
bool runtime_enabled = true;
ProfilerEventGroupMask enabled_event_groups = kProfilerAllEventGroups;
```

`ProfilerManager::Initialize(cfg)` 会应用这两个字段。即使 `ENGINE_PROFILER_ENABLED=OFF`，
stub 路径也会应用运行时状态，保持 API 行为一致。

### 5.2 ProfilerManager 控制接口

新增接口：

```cpp
void SetRuntimeEnabled(bool enabled);
bool IsRuntimeEnabled() const;

void SetEnabledEventGroups(ProfilerEventGroupMask mask);
void EnableEventGroups(ProfilerEventGroupMask mask);
void DisableEventGroups(ProfilerEventGroupMask mask);
void SetEventGroupEnabled(ProfilerEventGroup group, bool enabled);
bool IsEventGroupEnabled(ProfilerEventGroup group) const;
ProfilerEventGroupMask EnabledEventGroups() const;
```

示例：关闭物理和脚本桩：

```cpp
auto& profiler = engine::ProfilerManager::Get();
profiler.DisableEventGroups(
    engine::ProfilerEventGroupBit(engine::ProfilerEventGroup::Physics) |
    engine::ProfilerEventGroupBit(engine::ProfilerEventGroup::Script));
```

示例：只打开帧和定时器：

```cpp
auto mask =
    engine::ProfilerEventGroupBit(engine::ProfilerEventGroup::Frame) |
    engine::ProfilerEventGroupBit(engine::ProfilerEventGroup::Timer);
engine::ProfilerManager::Get().SetEnabledEventGroups(mask);
```

示例：运行中临时总关闭：

```cpp
engine::ProfilerManager::Get().SetRuntimeEnabled(false);
```

### 5.3 字符串解析辅助接口

新增：

```cpp
ProfilerEventGroupFromName(...)
ParseProfilerEventGroupMask(...)
FormatProfilerEventGroupMask(...)
```

示例：

```cpp
auto mask = engine::ParseProfilerEventGroupMask("physics,script,engine.aoi");
engine::ProfilerManager::Get().SetEnabledEventGroups(mask);
```

这为后续接入 admin HTTP、控制台命令、配置热更新留出入口。

## 6. 代码变更清单

新增：

- `src/runtime/profiler/profiler_switches.h`
- `src/runtime/profiler/profiler_switches.cc`

修改：

- `src/runtime/CMakeLists.txt`
  - 将 `profiler_switches.*` 纳入 `PROFILER_SOURCES`。
- `src/runtime/profiler/profiler_core.h`
  - `ProfilerConfig` 增加运行时开关字段。
  - `ProfilerManager` 增加运行时控制接口。
- `src/runtime/profiler/profiler_core.cc`
  - Initialize 应用运行时开关配置。
  - ON/OFF 两条路径都实现新接口。
- `src/runtime/profiler/profiler_macros.h`
  - 基础宏改为分组感知。
  - 新增 `ENGINE_PROFILE_*_GROUP`。
  - scope 使用自定义 RAII finalizer。
  - manual begin/end 使用线程本地栈。
- `src/runtime/profiler/profiler_events.h`
  - 高层业务宏改为显式分组。
- `src/tests/unit/profiler/test_profiler.cpp`
  - 增加分组解析、运行时开关、禁用时参数不求值测试。

## 7. 验证结果

Profiler ON：

```powershell
cmake -S src -B artifacts/build-profiler-switch `
  -DBUILD_TESTING=ON `
  -DENGINE_PROFILER_ENABLED=ON `
  -DENGINE_MONGODB_ENABLED=OFF `
  -DENGINE_PHYSICS_ENABLED=OFF `
  -DENGINE_MEM_STATS_ENABLED=OFF

cmake --build artifacts/build-profiler-switch --target test_profiler --config Debug -- /m
artifacts/bin/Debug/test_profiler.exe
```

结果：

- 生成了新的 `artifacts/bin/Debug/test_profiler.exe`。
- 测试通过：`All tests passed (29 assertions in 3 test cases)`。
- 该次 MSBuild 主命令因 node reuse 后台节点未退出触发外层超时，但链接产物已更新并可运行。

Profiler OFF：

```powershell
cmake -S src -B artifacts/build-profiler-switch-off `
  -DBUILD_TESTING=ON `
  -DENGINE_PROFILER_ENABLED=OFF `
  -DENGINE_MONGODB_ENABLED=OFF `
  -DENGINE_PHYSICS_ENABLED=OFF `
  -DENGINE_MEM_STATS_ENABLED=OFF

cmake --build artifacts/build-profiler-switch-off --target test_profiler --config Debug -- /m /nodeReuse:false
artifacts/bin/Debug/test_profiler.exe
```

结果：

- 构建成功。
- 测试通过：`All tests passed (16 assertions in 3 test cases)`。
- 构建过程中出现 libevent 第三方警告和 `LNK4098` 默认库冲突警告，和本次 profiler 开关改动无直接关系。

## 8. 性能与兼容性评估

### 8.1 热路径成本

Profiler 编译开启时，每个基础桩点新增：

- 一次 category 到 group 的轻量映射，或高层宏直接使用枚举常量。
- 一次 relaxed 原子读取总开关。
- 一次 relaxed 原子读取 group 位图。
- 一次位运算。

Profiler 编译关闭时：

- 所有宏仍为空操作。
- 参数仍不求值。
- 不链接 Perfetto。

### 8.2 和 Perfetto 原生 category 的关系

新增开关是 Perfetto 之前的业务门控：

```text
业务代码
  -> ENGINE_PROFILE_* 宏
     -> runtime group switch
        -> Perfetto TRACE_EVENT*
           -> Perfetto category/session switch
              -> trace buffer
```

这意味着：

- 运行时 group 关闭：Perfetto 宏不会被调用，参数不求值。
- 运行时 group 开启但 Perfetto session/category 未启用：仍由 Perfetto 原生逻辑跳过写入。
- 不修改 active session 的 `TrackEventConfig`，因此不会影响 Perfetto SDK 内部状态。

### 8.3 Begin/end 风险控制

scope 事件保存 begin 时状态，manual begin/end 使用线程本地栈，因此运行中开关切换不会造成
本层宏产生不匹配的 begin/end。

仍需注意：如果调用方手写不成对的 `ENGINE_PROFILE_BEGIN/END`，线程本地栈只能避免空 end 写入，
不能自动修复业务逻辑错误。

## 9. 后续建议

1. 接入 admin HTTP 或控制台命令：

```text
GET/POST /admin/profiler/groups?enabled=physics,script
POST /admin/profiler/runtime?enabled=false
```

2. 将 profiler 配置接入 runtime config：

```json
{
  "profiler": {
    "runtime_enabled": true,
    "enabled_event_groups": "frame,timer,physics"
  }
}
```

3. 如果需要比 group 更细的控制，可以在 `ProfilerEventGroup` 之上再加 event id 表：

```text
group: physics
event: PhysicsStep / Collision / Transform / Diff
```

当前 group 位图已经满足“针对某一批桩”的运行时批量控制；event id 适合后续定位某个模块内部的
单个热点事件时再加。

