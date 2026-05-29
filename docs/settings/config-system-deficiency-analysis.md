# 配置系统深度缺陷分析报告

**日期:** 2026-05-29
**修订:** R2 — 新增运维专家视角（第 7 章重写、新增第 9 章、优先级表更新）
**范围:** `src/runtime/config/`、`src/runtime/physics/physics_config.*`、`src/runtime/database/`、`src/runtime/engine/`、`src/server/`、配置消费侧全链路
**方法:** 逐文件审查 + 多角度交叉验证 + 运维场景模拟

---

## 目录

1. [架构层面](#1-架构层面)
2. [热更新机制](#2-热更新机制)
3. [校验体系](#3-校验体系)
4. [API 设计](#4-api-设计)
5. [线程安全](#5-线程安全)
6. [文件与格式](#6-文件与格式)
7. [可观测性与运维](#7-可观测性与运维)
8. [部署与生命周期管理](#8-部署与生命周期管理)
9. [安全与凭证管理](#9-安全与凭证管理)
10. [可测试性](#10-可测试性)
11. [问题汇总与优先级](#11-问题汇总与优先级)

---

## 1. 架构层面

### 1.1 双配置管理器并存，无统一抽象

当前存在两套完全独立的配置管理系统：

| 特性 | ConfigManager | PhysicsConfigManager |
|------|--------------|---------------------|
| 文件数 | 5+ JSON | 4 JSON |
| 单例模式 | Meyer's singleton | 普通类，由 PhysicsEngineBridge 持有 |
| 校验 | ConfigValidator（未调用，见 §3.1） | ValidateConfigs（内联，~90行） |
| 热更新 | Reload() 全量重载 | ReloadThresholds() / ReloadLogLevel() 逐字段 |
| 读取方式 | by-value copy + shared_lock | const& 直接返回（无锁） |
| glaze 反射 | 默认（snake_case key→member） | 显式 glaze::meta（camelCase key→snake_case member） |

**根因:** PhysicsConfigManager 是为物理子系统独立设计的，未复用 ConfigManager 的任何基础设施（文件读取、校验框架、回调通知）。两个管理器各行其道，新增第三个子系统（如 AI）时将面临"选择哪个模式"的困扰——选 ConfigManager 则依赖单例，选 PhysicsConfigManager 则需重写全套加载/校验/热更。

**影响:** 每增加一个需要配置的子系统，要么耦合到全局单例，要么复制一套私有配置管理器。参考现有模式，预期每个子系统约 300-400 行重复的加载/校验代码。

### 1.2 单例模式阻碍测试和多实例

`ConfigManager::Instance()` 是 Meyer's singleton：

```cpp
// config.cc:14-16
ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}
```

- **测试隔离失败:** 单元测试直接调用 `ConfigManager::Instance().LoadRuntimeFromString(...)` 修改全局状态，测试之间互相污染。TestFixture 通过 `ConfigFixture` 重复加载来"重置"状态，但这只是覆盖而非隔离。
- **无法多实例:** 无法在同一进程内运行两个独立的 Engine 实例（各自需要不同的配置），这在集成测试和库模式下是真实需求。

### 1.3 配置结构体设计缺乏分层

`RuntimeConfig` 承载了运行时的全部配置，从日志路径到物理场景路径全部扁平化：

```cpp
// config.h:48-55
struct RuntimeConfig {
    std::string resource_dir = config::kDefaultResourceDir;
    LogConfig log;
    FrameConfig frame;
    std::string scripts_dir = config::kDefaultRuntimeScriptsDir;
    std::string sandbox_level = "strict";
    std::string physics_scene_path = "/physics/data/scene.json";  // 物理路径为何在 runtime？
};
```

`physics_scene_path` 属于物理子系统的资产路径，却定义在 `RuntimeConfig` 中。当物理子系统被禁用时（`ENGINE_PHYSICS_ENABLED=OFF`），这个字段仍然存在且需要默认值。类似的，`sandbox_level` 是 VM 层面的配置，却放在顶层 `RuntimeConfig`。

**影响:** 子系统配置无法独立分发。一个子系统配置变更需要修改全局顶层结构体，违反开闭原则。

### 1.4 配置项散布在 ConfigManager 之外的独立结构体中

以下配置结构体不属于 ConfigManager 管辖，各有独立的创建/注入路径：

| 配置结构体 | 定义位置 | 注入方式 |
|-----------|---------|---------|
| `ProfilerConfig` | `profiler_core.h:17` | Engine::Init() 硬编码创建 |
| `SpaceConfig` | `space.h:30` | 由 Lua 脚本传入 |
| `ConnectorConfig` | `connector.h:16` | SetRetryConfig() setter |
| `DbServiceConfig` | `db_service_config.h:85` | ConfigManager::LoadDbServiceConfigFromFile() 静态方法 |

这些配置没有一个共同的"可加载/可校验/可热更"接口，各自为政。

---

## 2. 热更新机制

### 2.1 ConfigManager::Reload() 只完成一半工作

`Reload()` 的流程是：

```
1. 解析新 JSON → 临时对象（不修改当前配置）
2. 加写锁 → swap 配置对象
3. 通知 ReloadCallback
```

但 ReloadCallback 的实际行为仅仅是**打日志**（engine.cc:294-301）：

```cpp
ConfigManager::Instance().RegisterReloadCallback([]() {
    auto* logger = GetLogger();
    auto rt = ConfigManager::Instance().GetRuntimeConfig();
    ENGINE_LOG_INFO(logger,
        "config reloaded: frame_interval=[{}ms], log_level=[{}], sandbox=[{}]",
        rt.frame.interval_ms, rt.log.level, rt.sandbox_level);
});
```

**没有任何实际应用动作。** 帧率间隔（`frame_interval_`）在 Engine::Init() 中计算一次后就不再更新；日志级别不会动态变更；sandbox_level 只在 Init() 时使用。热更后的新配置值仅存在于 `ConfigManager` 内部，消费者看不到变更。

### 2.2 PhysicsConfigManager 的热更是选择性的

`PhysicsConfigManager` 提供两个热更方法：

- `ReloadThresholds()` — 只热更碰撞检测阈值
- `ReloadLogLevel()` — 只热更物理日志级别

`PhysicsConfig`（重力、求解器参数、最大刚体数等）和 `ThreadingConfig`（线程模型、任务队列大小）**不支持热更**——修改后必须重启物理线程。

### 2.3 Config 热更与 Script 热更无协调

`ConfigManager::Reload()` 和 `ScriptReloader` 是两个完全独立的事件循环：

```
ConfigManager::Reload()  →  NotifyReloadCallbacks()  →  仅打日志
ScriptReloader            →  FileWatcher              →  校验→清除缓存→重新加载
```

如果配置文件变更影响了脚本行为（如 `scripts_dir` 路径变更），脚本热更不会自动感知。反之，脚本热更也不会触发配置重载。

### 2.4 缺乏回滚机制

`Reload()` 的"失败保护"是：解析全部通过后才 swap。但如果新配置解析成功但**语义错误**（如 `target_fps=0` 但 `interval_ms=-1`），系统会直接应用这些非法值。没有版本化的配置快照用于回滚。

### 2.5 变更粒度不可知

ReloadCallback 的函数签名是 `std::function<void()>`——无参数。订阅者完全不知道**哪些字段**发生了变更，必须遍历对比全部配置项：

```cpp
// 假设的场景——当前 API 不支持这样做：
// callback([](const ConfigChangeSet& changes) { ... });
```

---

## 3. 校验体系

### 3.1 ConfigValidator 是死代码

`ConfigValidator::Validate()` 在 `config_validator.cc` 中实现，校验 `target_fps`、`interval_ms`、`resource_dir`、`scripts_dir`、`rotation_size_mb`，但**在全部 Load/Reload 路径中从未被调用**：

```
$ grep -rn "ConfigValidator::Validate" src/runtime/config/config.cc
(无结果)

$ grep -rn "ConfigValidator::Validate" src/runtime/
src/runtime/config/config_validator.cc:10:ConfigValidator::Result ConfigValidator::Validate(...)
(仅定义，无调用)
```

### 3.2 校验覆盖不完整

即使 ConfigValidator 被调用，其覆盖范围也严重不足：

| 配置结构体 | 字段数 | 校验状态 |
|-----------|-------|---------|
| RuntimeConfig | 6 (+ LogConfig 8 + FrameConfig 3) | ConfigValidator 覆盖 5 个字段 |
| ServerConfig | 6 (+ HttpConfig 1 + MsgpackConfig 2) | 无任何校验 |
| ClientConfig | 1 | 无任何校验 |
| MongoDbConfig | 嵌套 8 层，共 ~60 字段 | 无任何校验 |
| DbServiceConfig | 嵌套 4 层，共 ~12 字段 | 无任何校验 |

**现状:** `admin_port` 可以是 -1；`http.timeout_sec` 可以是 0 或负数；`msgpack.max_payload_size` 可以是 0（导致拒绝所有消息）；`db_service.connection_pool.wait_queue_timeout_ms` 为 0 时无限阻塞（头文件注释中明确标为 "DANGEROUS"）。

### 3.3 校验模式不统一

两套配置管理器使用了完全不同的校验模式：

```cpp
// ConfigValidator: 返回 Result{valid, errors} 结构体
ConfigValidator::Result r = ConfigValidator::Validate(config);
if (!r.valid) { /* 处理 r.errors */ }

// PhysicsConfigManager: 返回 bool + 输出参数
std::string error;
if (!ValidateConfigs(error)) { /* 处理 error */ }
```

### 3.4 缺少跨字段校验

- `interval_ms` 和 `target_fps` 存在恒等关系（interval_ms = 1000 / target_fps），两个字段可独立设置，不一致时无告警
- `max_pool_size` (16) 和 `thread_count` (4) 约束为 `max_pool_size >= thread_count`，违反时无校验
- `mongodb_dev` 和 `mongodb_public` 同时为空时系统静默退化，无告警

---

## 4. API 设计

### 4.1 不可变访问返回 by-value copy

```cpp
RuntimeConfig GetRuntimeConfig() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return runtime_config_;  // 完整拷贝
}
```

`MongoDbConfig` 包含 8 个嵌套结构体、多个 `std::vector`、`std::string`。每次 `GetMongoDbDevConfig()` 调用都是一次深拷贝。虽然配置读取不在热路径上，但 API 设计暗示"可以频繁调用"，调用者可能不知情地在循环中使用。

### 4.2 可变访问绕过锁

```cpp
RuntimeConfig& GetRuntimeConfigMutable() {
    return runtime_config_;  // 无锁，无文档说明调用方需要外部同步
}
```

这个方法是 public API，被 `server.cc:67-70` 用于 CLI 参数覆盖：

```cpp
ConfigManager::Instance().GetRuntimeConfigMutable().log.dir = arg.substr(10);
```

此调用发生在 Engine::Init() 之前（单线程上下文），所以当前是安全的。但 API 本身不阻止在运行时从任意线程调用，一旦误用就会产生数据竞争。

### 4.3 静态方法与实例方法混用

```cpp
// 实例方法
bool LoadRuntimeFromFile(const std::string& path);

// 静态方法
static bool LoadMongoDbConfigFromFile(const std::string& path, MongoDbConfig& out);
static bool LoadDbServiceConfigFromFile(const std::string& path, DbServiceConfig& out);
```

`LoadMongoDbConfigFromFile` 和 `LoadDbServiceConfigFromFile` 是静态方法，不依赖 `ConfigManager` 的任何内部状态。但 `LoadRuntimeFromFile` 是实例方法（解析到 `this->runtime_config_`）。同一语义的不同实现方式让调用者困惑——"加载一个 DbServiceConfig 我应该用 ConfigManager 的实例还是直接调静态方法？"

### 4.4 MongoDB 配置的命名不一致

```cpp
// config.h:73-77（原文注释）
// NOTE: Member names use camelCase to match the source JSON keys directly
// (glaze auto-reflection matches member names to JSON keys). This differs
// from the rest of the config structs because the mongodb config files
// were authored in camelCase.
struct MongoDbConnectionOptions {
    std::string readPreference = "primaryPreferred";  // camelCase C++ 成员
    int maxPoolSize = 10;
    // ...
};
```

其他所有配置使用 snake_case 成员名（`rotation_size_mb`、`target_fps`），而 MongoDB 配置使用 camelCase。这破坏了一致性——开发者需要记住"读 MongoDB 配置成员时切换到 camelCase"。

### 4.5 缺少 const 正确性标记

```cpp
// PhysicsConfigManager — 全部返回 const&（好）
const PhysicsConfig& GetPhysicsConfig() const { return physics_config_; }

// 但 GetThresholdsConfigMutable 返回非 const 引用（坏）
ThresholdsConfig& GetThresholdsConfigMutable() { return thresholds_config_; }
```

`GetThresholdsConfigMutable()` 的存在理由是热更时直接赋值。但一个 public mutable 访问器就是一把上膛的枪——任何代码都可以绕过所有校验直接修改配置值。

---

## 5. 线程安全

### 5.1 config_mutex_ 的粒度问题

`config_mutex_` 是一个全局读写锁，保护 `runtime_config_`、`client_config_`、`server_config_`、`mongo_dev_config_`、`mongo_public_config_` 五个独立的结构体。读取其中任何一个都需要获取同一个 shared_lock：

```cpp
// 线程 A 读取 runtime config
auto rt = cfg.GetRuntimeConfig();  // 获取 shared_lock

// 线程 B 想读取 mongo config（与 runtime config 无关）
auto mongo = cfg.GetMongoDbDevConfig();  // 等待 shared_lock（被线程 A 阻塞）
```

一个粗粒度锁保护了所有配置类型。如果有高频读取某类配置的需求，它会被其他无关配置的读写阻塞。

### 5.2 callbacks_mutex_ 用 shared_mutex 却只做排他锁定

```cpp
mutable std::shared_mutex callbacks_mutex_;  // shared_mutex
// ...
void NotifyReloadCallbacks() {
    std::shared_lock<std::shared_mutex> lock(callbacks_mutex_);  // 共享锁
    callbacks_copy = callbacks_;  // 拷贝
}
// ...
int RegisterReloadCallback(ReloadCallback callback) {
    std::lock_guard<std::shared_mutex> lock(callbacks_mutex_);  // 排他锁
    // ...
}
```

`NotifyReloadCallbacks` 使用 shared_lock 进行读操作，这是正确用法。但 `callbacks_` 是一个 `std::vector`，其拷贝操作在 shared_lock 下进行——如果在拷贝过程中另一个线程调用 `RegisterReloadCallback`（需要排他锁），会阻塞写者。这是预期行为，但设计上过度了：回调注册是低频操作，用 `std::mutex` 更简单且无额外开销。

### 5.3 MongoDB 路径访问的 TOCTOU

```cpp
std::string ConfigManager::GetMongoDbDevPath() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return server_config_.mongodb_dev;  // 返回值的拷贝
}
// lock 释放，返回值是调用方的本地副本

// 调用方:
std::string path = cfg.GetMongoDbDevPath();  // 获取路径
if (!path.empty()) {
    // 此处 path 指向的文件可能已被其他线程通过 SetMongoDbDevPath 改变
    cfg.LoadMongoDbConfigFromFile(path, out);  // 加载"旧路径"的文件
}
```

这不是当前代码中的实际 bug（SetMongoDbDevPath 目前只在初始化阶段调用），但 API 设计鼓励这种使用模式。

### 5.4 Reload() 与 LoadRuntimeFromString() 之间的竞争

两个方法都直接写入 `runtime_config_`：

```cpp
// LoadRuntimeFromString — 无锁写入
bool ConfigManager::LoadRuntimeFromString(const std::string& json) {
    auto ec = glz::read_json(runtime_config_, json);  // 直接写入成员
    // ...
}

// Reload — 有锁 swap
bool ConfigManager::Reload(const std::string& config_dir) {
    RuntimeConfig new_runtime;
    glz::read_file_json(new_runtime, ...);  // 写入临时对象
    {
        std::lock_guard<std::shared_mutex> lock(config_mutex_);
        runtime_config_ = std::move(new_runtime);  // 加锁 swap
    }
}
```

`LoadRuntimeFromString` 不加锁直接写入 `runtime_config_`——如果另一个线程正在 `GetRuntimeConfig()` 的 shared_lock 下拷贝，会产生数据竞争。

---

## 6. 文件与格式

### 6.1 JSON 键命名三种风格并存

| 文件 | JSON 键风格 | C++ 成员风格 | 映射方式 |
|------|-----------|-------------|---------|
| `runtime/runtime.json` | snake_case | snake_case | 自动（名称一致） |
| `server/server.json` | snake_case | snake_case | 自动（名称一致） |
| `server/db_service.json` | snake_case | snake_case | 自动（名称一致） |
| `server/mongodb_dev.json` | camelCase | camelCase | 自动（名称一致，但与其他不一致） |
| `physics/physics.json` | camelCase | snake_case | 手动（glaze::meta 显式映射） |
| `physics/threading.json` | camelCase | snake_case | 手动（glaze::meta 显式映射） |

开发者在手写 JSON 配置文件时需要记住当前文件"应该用 snake_case 还是 camelCase"。物理配置文件没有 schema 文档，字段映射关系隐藏在 C++ 的 `glaze::meta` 模板特化中——这本质上是编译期常量，IDE 无法提供补全或校验。

### 6.2 配置目录结构无版本控制

```
resources/config/
├── runtime/runtime.json
├── server/server.json
├── server/db_service.json
├── server/mongodb_dev.json
├── server/mongodb_public.json
├── client/client.json
└── physics/configs/
    ├── physics.json
    ├── threading.json
    ├── logging.json
    └── thresholds.json
```

- 没有 schema 版本字段（如 `"version": 2`），无法在加载时检测格式兼容性
- 没有默认值文档——默认值分散在 C++ 结构体初始化器中，没有 .json 模板文件
- physics 配置使用复数 `configs/`，顶层使用单数 `config/`——命名不一致

### 6.3 硬编码路径缺乏环境变量支持

```cpp
// config_constants.h:8
inline constexpr const char* kConfigDir = "resources/config";
```

唯一的覆盖路径是 `--config_dir=` CLI 参数（server.cc:50-54）。没有环境变量（如 `EVPP_CONFIG_DIR`）支持，没有 XDG 风格路径查找（`$HOME/.evpp/config`、`/etc/evpp/config`）。

### 6.4 MongoDB 配置文件的特殊处理

MongoDB 配置文件包含 `_description` 和 `_updated` 字段（以下划线开头），这在 glaze 自动反射中需要 C++ 成员名以下划线开头（`_description`），而这是 C++ 的保留命名模式（`_Uppercase` 和 `__` 是保留标识符）。

---

## 7. 可观测性与运维

### 7.1 Reload 日志信息量为零

```cpp
// config.cc:174
ENGINE_LOG_INFO(logger, "ConfigManager: config reloaded");
```

这条日志不包含：哪些文件被重载、哪些字段发生了变更、变更前后的值、重载耗时。运维人员看到这条日志时无法判断"配置重载是否按预期生效"。在发生配置相关的生产故障时，日志中唯一的信息是一条"已重载"消息，完全无法用于根因分析。

**对比行业实践:** etcd/consul 等配置中心的变更事件包含完整的 key-level diff；nginx -s reload 会在 error log 中记录每个 worker 的重载状态。本系统的日志水平停留在"printf 级别的确认消息"。

### 7.2 无配置导出/快照 API

没有 `ConfigManager::Dump()` 或 `ConfigManager::GetEffectiveConfig()` 方法将当前运行中的配置序列化回 JSON。当出现配置相关故障时，无法快速获取"引擎实际使用的配置值"来与磁盘文件对照。

关键运维场景无法满足：
- **故障排查:** 磁盘上的 JSON 文件可能在进程启动后被修改（或通过 CLI 覆盖），"当前生效配置"与"磁盘文件内容"可能不一致
- **配置审计:** 无 API 能回答"这台服务器上 `frame.target_fps` 的实际值是多少"
- **自动化巡检:** 运维脚本无法通过 API 拉取运行中配置与变更管理系统（如 Ansible/Chef）的预期值做 drift 检测

### 7.3 无 dry-run 模式

没有 `ConfigManager::ValidateOnly(path)` 或 `ConfigManager::Reload(path, dry_run=true)` 来仅校验文件而不应用。运维人员在应用新配置前无法安全地测试其正确性。

**实际运维场景:** 凌晨 2 点通过配置管理工具推送了新的 `server.json`，但其中 `admin_port` 写成了字符串 `"8081"` 而非数字 `8081`。在生产服务器上执行 Reload 才会发现解析失败——而原始的合法配置已经被持有，新配置的解析错误只被记录为一条 ERROR 日志，运维人员可能完全错过。

### 7.4 健康检查端点不验证依赖项

当前 `/health` 端点（admin_http.cc:35-43）的实现：

```cpp
void HandleHealth(...) {
    oss << "{\"status\":\"ok\",\"uptime_seconds\":" << UptimeSeconds()
        << ",\"frame_count\":" << engine::Engine::Instance().frame_count()
        << ",\"running\":" << (engine::Engine::Instance().running() ? "true" : "false")
        << "}";
}
```

只检查 `Engine::running()` 和 `frame_count`。**不验证任何后端依赖项的状态**：

- 数据库连接池是否健康？（`DatabaseService::IsHealthy()` 存在但未被调用）
- 物理模拟线程是否正常运行？
- MongoDB 连接是否存活？
- DBThread 的 healthy_ 标志是否均为 true？

在 Kubernetes 环境中，这意味着：
- **Liveness probe 缺失:** 一个 event loop 仍在运行但数据库已断开的进程会被判定为"健康"，Kubernetes 不会重启它
- **Readiness probe 缺失:** 进程启动后 event loop 立即就绪，但数据库连接池可能需要 5-10 秒建立连接。在此期间进入的流量会遇到数据库错误

**Readiness vs Liveness 区分是容器编排环境的基本要求，当前实现完全不具备。**

### 7.5 Profiler 配置是硬编码的

```cpp
// engine.cc:121-122
ProfilerConfig prof_cfg;
prof_cfg.buffer_size_kb = 32768;  // 硬编码，无配置文件
ProfilerManager::Get().Initialize(prof_cfg);
```

性能分析器的缓冲区大小（32MB）、输出路径、刷新间隔（5s）、写入模式全部硬编码。在以下运维场景中完全不可用：
- 定位生产环境性能瓶颈时需要增大缓冲区（长时间采样）或减小（减少内存占用）
- 容器化环境中输出路径 `trace.perfetto-trace` 可能指向不可写的文件系统
- 需要在 CI 中启用 profiler 进行自动化性能回归检测时，必须修改源码重新编译

### 7.6 管理端点无访问控制

`/health`、`/stats`、`/metrics` 三个端点在 `admin_port`（默认 8081）上公开，无任何认证：
- `/metrics` 导出 Prometheus 格式指标，包含内部计数器值——攻击者可借此推断服务器负载模式
- `/stats` 导出连接数、消息吞吐量——相当于免费的业务情报
- 无 TLS 支持（admin_http.cc 使用纯 HTTP）
- 无速率限制——单个客户端可以无限频率轮询

**安全影响:** 如果 `admin_port` 被错误地绑定到 `0.0.0.0` 而非 `127.0.0.1`（当前无绑定地址配置），任何人都可以访问管理端点。

### 7.7 资源配置限制是编译期常量

```cpp
// limits.h:14-24
static constexpr uint32_t kDefaultMaxMessageSize = 64 * 1024;       // 64 KiB
static constexpr uint32_t kDefaultMaxBufferCapacity = 256 * 1024;   // 256 KiB
static constexpr uint32_t kDefaultMaxHttpBodySize = 10 * 1024 * 1024; // 10 MiB
static constexpr uint32_t kDefaultMaxMsgpackDepth = 64;
```

这些是关键的 DoS 防护参数。但作为 `static constexpr`，它们需要在编译时确定。运维团队无法：
- 为高吞吐量场景临时提升消息大小限制
- 在受到攻击时紧急降低限制
- 为不同服务类型（HTTP API / 游戏服务器）设置不同的限制

### 7.8 无 Prometheus metrics 暴露配置变更事件

`MetricsRegistry` 有 `connections_total`、`messages_received_total` 等业务指标，但没有任何关于配置系统本身的指标：
- `config_reload_total`（配置重载次数）
- `config_reload_errors_total`（配置重载失败次数）
- `config_reload_duration_seconds`（重载耗时直方图）
- `config_hash`（当前配置内容哈希，用于检测 drift）

缺少这些指标意味着配置系统本身是一个"黑盒"——运维团队无法建立配置变更的 SLO 监控。

---

## 8. 部署与生命周期管理

### 8.1 Dev/Prod 环境切换是编译期行为（严重）

`engine.cc:178-182` 包含一个致命的运维反模式：

```cpp
#ifndef NDEBUG
    auto mongo_cfg = ConfigManager::Instance().GetMongoDbDevConfig();
#else
    auto mongo_cfg = ConfigManager::Instance().GetMongoDbPublicConfig();
#endif
```

**Dev 数据库还是生产数据库的选择由 `NDEBUG` 宏决定——这是一个编译期常量。** 这意味着：

| 影响 | 详情 |
|------|------|
| **无法多环境部署同一二进制** | 同一个 Release 构建在 staging 环境和 production 环境行为不同——但 staging 应该用 dev DB，而 Release 构建强制使用 public DB |
| **无法运行时切换** | 配置文件中同时有 `mongodb_dev` 和 `mongodb_public` 两个字段，但选择哪个是硬编码的。没有 `--env=staging` 或 `EVPP_ENV=production` 运行时覆盖 |
| **回滚风险** | 生产环境需要紧急切换到备份数据库时，必须重新编译——这在凌晨 3 点的故障场景中是不可接受的 |
| **CI/CD 不友好** | 同一个 Docker 镜像在 staging 通过测试后推送到 production，行为却因编译选项不同而改变——这违反了"构建一次，到处部署"的基本原则 |

`server.json` 中同时存在 `mongodb_dev` 和 `mongodb_public` 两个路径，暗示设计时考虑了多环境，但实际的选择逻辑被硬编码在 C++ 中。

### 8.2 缺少 SIGHUP 配置重载（Unix 运维标准缺失）

信号处理（engine.cc:322-344）仅覆盖了关闭信号：

```cpp
sigint_watcher_  = ... SIGINT  → Shutdown()
sigterm_watcher_ = ... SIGTERM → Shutdown()
```

**完全没有 SIGHUP 处理。** 在 Unix 服务器运维中，`kill -HUP <pid>` 是触发配置重载的标准方式。每个主流服务器都支持：

| 服务器 | SIGHUP 行为 |
|--------|------------|
| nginx | 重载配置，优雅重启 worker |
| PostgreSQL | 重载 pg_hba.conf 和 postgresql.conf |
| Redis | 重载 redis.conf |
| HAProxy | 重载 haproxy.cfg |
| Envoy | 热重启（hot restart） |

本系统的 `ConfigManager::Reload()` 已经实现了配置重载的核心逻辑，只缺一个 SIGHUP 信号处理器将其连接到运维标准接口。这个遗漏意味着：
- 配置管理工具（Ansible、Salt、Chef）无法用标准方式触发重载
- 容器编排的 `postStart` hook 无法用 `kill -HUP 1` 激活新配置
- 运维团队需要自定义 HTTP 端点或 CLI 命令来触发重载

### 8.3 无连接优雅排空（Graceful Connection Draining）

关闭序列（engine.h:44-56）的 6 个阶段是：

```
1. Physics Shutdown → 2. Database Shutdown → 3. Network Shutdown
→ 4. Timer Shutdown → 5. Script Destroyed → 6. Final Logs
```

**没有连接排空阶段。** 在"Network Shutdown"阶段，RPC 绑定层会排空待处理队列（`DrainPendingQueue`），但 TCP 连接层没有给已连接客户端发送关闭通知或等待正在进行的消息完成的机会。

生产环境中这意味着：
- 客户端感知到的不是"服务器优雅关闭"，而是"连接突然断开"（TCP RST 而非 FIN）
- 正在处理的玩家请求（如交易、存档）可能会丢失
- WebSocket/长连接客户端无法收到 `close` 帧

**对比行业实践:** Kubernetes 的 `terminationGracePeriodSeconds` 依赖应用在收到 SIGTERM 后进入"不再接受新连接 + 排空现有连接"的状态。当前实现无法与 K8s Pod 终止流程正确集成。

### 8.4 无可配置的关闭超时

`Shutdown()` 设置 `running_=false` 并停止 event loop，然后 `Cleanup()` 顺序销毁各子系统。每个子系统的销毁都没有超时限制：

```cpp
PhysicsEngineBridge::Instance().Shutdown();  // 物理线程 join —— 可能永远阻塞
DatabaseService::Instance().Shutdown();        // DB 线程排空 —— 可能永远阻塞
```

如果物理模拟陷入死循环，`Shutdown()` 会永远挂起。没有 `shutdown_timeout_sec` 配置项来限制总关闭时长。

在容器环境中，Kubernetes 会在 `terminationGracePeriodSeconds`（默认 30s）后发送 SIGKILL。如果 Cleanup() 耗时超过这个值，进程会被强制杀死——丢失所有未完成的工作（日志缓冲区、profiler trace、未刷新的数据库写入）。

### 8.5 无 PID 文件管理

服务器进程不写 PID 文件，不使用文件锁防止多实例冲突。两个进程可以同时启动在同一配置目录下，竞争同一日志目录和数据库连接。

运维影响：
- **进程管理:** systemd/upstart/supervisord 等进程管理器通常依赖 PID 文件来跟踪进程
- **多实例防护:** 没有机制防止运维人员意外启动第二个实例
- **健康检查脚本:** `kill -0 $(cat server.pid)` 是检查进程存活的常用方式

### 8.6 启动失败无结构化错误码

`server.cc:56-58`:

```cpp
if (!engine::ConfigManager::Instance().Load(config_dir)) {
    std::cerr << "Failed to load config from " << config_dir << std::endl;
    return 1;
}
```

所有配置加载失败都返回退出码 1。没有区分：
- 退出码 2: 配置文件不存在（ENOENT）
- 退出码 3: JSON 语法错误
- 退出码 4: 配置校验失败（合法 JSON 但语义非法）
- 退出码 5: 文件权限不足（EACCES）

监控系统（如 Kubernetes liveness probe 基于退出码判断 crash loop）无法区分"配置文件写错了"和"磁盘故障"——两者都是退出码 1。

### 8.7 日志初始化在配置校验之前（静默失败风险）

`Engine::Init()` 的执行顺序（engine.cc:109-115）：

```
1. InitLogger(runtime_cfg.log)   ← 使用未经校验的 log 配置
2. (后续) 各种子系统初始化
3. 无任何时机调用 ConfigValidator::Validate()
```

如果 `runtime_cfg.log.dir` 指向 `/var/log/readonly_dir/`（目录存在但无写权限），`InitLogger` 可能会静默失败（取决于 quill 库的行为），后续所有 `ENGINE_LOG_*` 调用都会丢失。更糟的是，没有任何一个步骤会捕捉"日志系统初始化是否成功"的反馈并告警。

### 8.8 缺少配置包含/覆盖机制

所有 JSON 配置文件是独立的平面文件。没有：

- **层级覆盖:** 无法用 `server.prod.json` 覆盖 `server.json` 中的部分字段
- **配置包含:** 无法在 `runtime.json` 中写 `"include": ["common.json"]`
- **环境变量插值:** 无法写 `"log_dir": "${EVPP_LOG_DIR:-/var/log/evpp}"`

这意味着：
- 多环境管理（dev/staging/prod）需要维护多份完整的配置文件副本
- 配置文件中出现重复内容（如所有环境共享的 `msgpack` 配置需要在每个环境文件中复制）
- 敏感信息（密码、密钥）必须嵌入 JSON 文件，无法通过环境变量注入

### 8.9 CleanupPhase 不对外可见

Engine 定义了 6 个明确的清理阶段（CleanupPhase 枚举），但没有任何方式从外部查询当前处于哪个阶段。`/health` 端点在清理过程中仍然返回 `"running": false` 但无法区分：

- "刚收到 SIGTERM，正在排空连接"（CleanupPhase::NetworkShutdown）
- "正在等待数据库线程退出"（CleanupPhase::DatabaseShutdown）
- "已完成所有清理，即将退出"（CleanupPhase::FinalLogs）

在容器编排中，这阻碍了正确的 `preStop` hook 设计——无法让负载均衡器在排空连接期间移除 Pod，而在数据持久化期间等待。

### 8.10 无启动探针（Startup Probe）区分

当前健康检查端点是一个二元开关：event loop 启动后返回 `"status":"ok"`，启动前端口未绑定。

Kubernetes 需要三种探针：
- **Startup probe:** "应用是否已完成初始化？"——在数据库连接、脚本加载、物理引擎初始化全部完成后才返回成功
- **Liveness probe:** "应用是否还活着？"——event loop 运行 + 关键子系统健康
- **Readiness probe:** "应用是否可以接受流量？"——所有依赖项就绪

当前实现将三者混为一体，无法正确配置容器编排的健康检查策略。特别是，初始化阶段（Engine::Init）可能需要 10-30 秒（数据库连接、物理引擎加载），但 Kubernetes 默认的 `initialDelaySeconds` 是一个粗粒度的猜测值。

---

## 9. 安全与凭证管理

### 9.1 MongoDB 连接凭证明文存储

MongoDB 配置文件（如 `mongodb_dev.json`）将完整连接 URI 存储在明文 JSON 中：

```json
"connection": {
    "uri": "mongodb://127.0.0.1:27017,127.0.0.1:27018,127.0.0.1:27019/?replicaSet=rs0",
```

当前开发环境未启用认证（`"authentication": false`）。一旦启用认证，URI 将变为：

```
mongodb://admin:SuperSecret123@host1:27017,host2:27018/?replicaSet=rs0&authSource=admin
```

**密码以明文形式写入磁盘文件。** 这违反了：
- OWASP Top 10 A07:2021（Identification and Authentication Failures）
- SOC 2 密码管理要求
- PCI DSS Requirement 8（如涉及支付数据）

### 9.2 无外部 Secret 注入机制

系统完全不支持从外部 Secret 管理系统获取凭证：

- 无 Kubernetes Secret 挂载支持
- 无 HashiCorp Vault 集成
- 无 AWS Secrets Manager / GCP Secret Manager 支持
- 连最简单的环境变量覆盖都不支持（MongoDB 配置是整体加载的，无法用 `${MONGO_PASSWORD}` 替换 URI 中的密码字段）

### 9.3 admin_port 绑定地址不可配置

`AdminHttpServer::Start()` 始终绑定到 `0.0.0.0:<port>`（evpp HTTP Service 默认行为），没有 `admin_bind_address` 配置项将其限制为 `127.0.0.1`。管理端点意外暴露到公网的风险无法通过配置消除。

### 9.4 无配置签名/完整性校验

JSON 配置文件没有任何完整性保护——无校验和、无数字签名、无 HMAC。如果文件被意外损坏（磁盘位翻转）或恶意篡改，系统会静默加载损坏的配置（只要 glaze 能解析出部分 JSON）。

---

## 10. 可测试性

### 10.1 测试污染全局单例

```cpp
// test_config.cpp
TEST_CASE("ConfigManager rejects invalid JSON", "[config][error]") {
    auto& cfg = engine::ConfigManager::Instance();
    REQUIRE_FALSE(cfg.LoadRuntimeFromString(""));  // 修改全局状态
}
```

每个测试用例都直接修改全局 ConfigManager 的状态。测试执行顺序依赖 Catch2 的随机种子——同一测试套件在不同运行中可能因为顺序不同而通过或失败。`ConfigFixture::LoadFromStrings()` 试图通过在 setUp 中重载来"重置"状态，但这只是掩盖而非解决根本问题。

### 10.2 无 mock/fake 接口

ConfigManager 没有抽象接口（IConfigManager），`PhysicsConfigManager` 也没有抽象接口。所有配置消费者直接依赖具体类，无法在单元测试中注入 mock 配置。

### 10.3 fuzz 测试不覆盖文件路径

`config_parse_fuzz.cpp` 只测试 `LoadRuntimeFromString`、`LoadClientFromString`、`LoadServerFromString`。没有覆盖文件加载路径（`LoadRuntimeFromFile`）和 Reload 路径。文件 I/O 相关的缓冲区溢出、路径遍历、BOM 处理错误无法通过模糊测试检测。

### 10.4 基准测试不反映真实场景

```cpp
// bench_config.cpp
static void BM_Config_ParseRuntime(benchmark::State& state) {
    // 只测试 parse，不测试文件读取、校验、回调通知
    for (auto _ : state) {
        engine::ConfigManager::Instance().LoadRuntimeFromString(json);
    }
}
```

仅测量了 glaze JSON 解析速度，未覆盖 Load/Reload 的完整路径。

---

## 11. 问题汇总与优先级

### P0 — 阻塞上线（不解决则不可在生产环境运行）

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| P0-1 | `ConfigValidator::Validate()` 从未被调用 | `config_validator.cc:10` | 非法配置静默接受，运行时行为未定义 |
| P0-2 | `LoadRuntimeFromString` 无锁写入 | `config.cc:22` | 与 `GetRuntimeConfig()` 数据竞争，UB |
| P0-3 | 配置热更后消费者不感知变更 | `engine.cc:294-301` | Reload 功能形同虚设，回调只打日志 |
| **P0-4** | **Dev/Prod DB 选择是编译期行为** | `engine.cc:178-182` | **同一二进制无法多环境部署；回滚需重编译** |
| **P0-5** | **MongoDB 凭证明文存储在 JSON 文件中** | `mongodb_dev.json:13` | **密码泄露风险，违反安全合规** |
| **P0-6** | **健康检查不验证后端依赖** | `admin_http.cc:35-43` | **K8s 无法正确判断 Pod 健康状态** |

### P1 — 严重影响开发与运维效率

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| P1-1 | 双配置管理器无统一抽象 | `config.h` / `physics_config.h` | 每新增子系统 300+ 行重复代码 |
| P1-2 | 全局单例阻碍测试隔离 | `config.cc:14` | 测试随机失败，不可重现 |
| P1-3 | 可变访问器绕过线程安全 | `config.h:235-253` | 运行时误用导致数据竞争 |
| P1-4 | 无跨字段校验 | 全局 | 逻辑不一致的配置组合静默接受 |
| P1-5 | MongoDB 配置使用 camelCase（命名规范分裂） | `config.h:79-163` | C++ 成员名与项目规范不一致 |
| P1-6 | ProfilerConfig 硬编码 | `engine.cc:121-122` | 调整 profiler 需重新编译 |
| **P1-7** | **ResourceLimits 全部为编译期常量** | `limits.h:14-24` | **DoS 防护参数无法运行时调整，无法应对攻击** |
| **P1-8** | **缺少 SIGHUP 配置重载** | `engine.cc:322-344` | **违反 Unix 服务器运维标准惯例** |
| **P1-9** | **无连接优雅排空** | `engine.h:44-56` | **客户端在服务器关闭时连接被暴力断开** |
| **P1-10** | **无可配置关闭超时** | `engine.cc:392-402` | **子系统 hang 住时 Cleanup() 永远阻塞，K8s SIGKILL 丢数据** |

### P2 — 影响运维质量与安全

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| P2-1 | Reload 日志不含变更详情 | `config.cc:174` | 无法审计配置变更 |
| P2-2 | 无配置导出/快照 API | N/A | 无法确认运行中配置的实际值 |
| P2-3 | 无 dry-run 校验模式 | N/A | 无法安全预检新配置 |
| P2-4 | 无配置 schema 版本号 | 全部 JSON 文件 | 版本迁移无检测机制 |
| P2-5 | 三种 JSON 键命名风格并存 | 全部 JSON 文件 | 配置文件编写容易出错 |
| P2-6 | 无 PID 文件/多实例防护 | `server.cc:42-84` | 可能意外启动多实例，资源竞争 |
| P2-7 | 启动失败无结构化错误码 | `server.cc:56-58` | 监控系统无法区分故障模式 |
| P2-8 | admin_port 绑定地址不可配置 | `admin_http.cc:76-97` | 管理端点可能意外暴露到公网 |
| P2-9 | 无外部 Secret 注入支持 | 全局 | 密码管理依赖明文文件 |
| P2-10 | 缺少 Prometheus 配置相关指标 | `metrics.h` | 配置系统本身不可观测 |

### P3 — 可改进项

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| P3-1 | `callbacks_mutex_` 应使用 `std::mutex` | `config.h:323` | 不必要的 shared_mutex 开销 |
| P3-2 | Config 热更与 Script 热更无协调 | `engine.cc` | 配置变更不触发脚本重载 |
| P3-3 | `physics_scene_path` 不应在 RuntimeConfig | `config.h:54` | 违反子系统边界 |
| P3-4 | 缺少环境变量插值支持 | `config_constants.h` | 容器化部署不友好 |
| P3-5 | fuzz 不覆盖文件 I/O 路径 | `config_parse_fuzz.cpp` | 文件解析漏洞检测盲区 |
| P3-6 | 物理配置目录用复数 `configs/` | 目录结构 | 命名不统一 |
| P3-7 | 日志初始化在配置校验之前 | `engine.cc:114` | 日志目录无写权限时静默失败 |
| P3-8 | CleanupPhase 不对外可见 | `engine.h:57-66` | 无法向负载均衡器报告关闭进度 |
| P3-9 | 缺少 Startup/Readiness/Liveness 探针区分 | `admin_http.cc:35-43` | 容器编排健康检查粗粒度 |
| P3-10 | 缺少配置包含/覆盖机制 | 全部 JSON 文件 | 多环境管理需要复制完整配置文件 |

---

## 附录 A: 配置数据流全链路

```
server.cc main()
  │
  ├─ ConfigManager::Instance().Load("resources/config")
  │   ├─ LoadRuntimeFromFile  →  glaze::read_file_json  →  runtime_config_
  │   ├─ [optional] LoadClientFromFile  →  client_config_
  │   ├─ [optional] LoadServerFromFile  →  server_config_
  │   │   └─ LoadMongoDbConfigsFromServer  →  mongo_dev_config_ / mongo_public_config_
  │   └─ ConfigValidator::Validate  ←  ⚠️ 从未调用
  │
  ├─ CLI 覆盖: GetRuntimeConfigMutable().log.dir = ...
  │
  ├─ Engine::Init(runtime_cfg, scripts_dir)
  │   ├─ InitLogger(runtime_cfg.log)  ← 未经校验的日志配置
  │   ├─ ProfilerManager::Get().Initialize(prof_cfg)  ← 硬编码
  │   ├─ DatabaseService::Initialize(db_svc_config, uri)
  │   │   ├─ #ifndef NDEBUG → dev config, #else → public config  ← 编译期选择！
  │   │   └─ ConfigManager::LoadDbServiceConfigFromFile(server_cfg.db_service, ...)
  │   ├─ PhysicsEngineBridge::Initialize(phys_cfg, phys_data, scripts_dir)
  │   │   └─ PhysicsConfigManager::Load(config_dir)
  │   │       ├─ LoadPhysics("physics.json")
  │   │       ├─ LoadThreading("threading.json")
  │   │       ├─ LoadLogging("logging.json")
  │   │       ├─ LoadThresholds("thresholds.json")
  │   │       └─ ValidateConfigs()  ← ✅ 实际调用
  │   ├─ ConfigManager::RegisterReloadCallback([](){ /* 仅打日志 */ })
  │   └─ ScriptReloader::Start()  ← 独立热更系统
  │
  └─ Engine::Run()
      ├─ Start()  →  frame_timer + SIGINT/SIGTERM watchers  ← 无 SIGHUP！
      └─ FrameLoop()
          ├─ ConfigManager::GetServerConfig().msgpack.max_nesting_depth  (每帧)
          └─ ConfigManager::GetServerConfig().http.timeout_sec  (每请求)
```

## 附录 B: 关闭生命周期序列

```
SIGINT / SIGTERM / Shutdown()
  │
  ├─ running_ = false  →  event loop 退出
  │
  ├─ Phase 1: Physics Shutdown     — PhysicsThread join，无超时
  ├─ Phase 2: Script Reloader Stop — 停止 FileWatcher，释放 Lua 引用
  ├─ Phase 3: Database Shutdown    — DBThread join + 排空响应队列，无超时
  ├─ Phase 4: Network Shutdown     — RPC DrainPendingQueue（"shutdown" 错误），
  │                                   TCP 连接直接断开，无通知
  ├─ Phase 5: Timer Shutdown       — TimerManager 取消所有定时器
  ├─ Phase 6: Script Destroyed     — Lua GC + VM 销毁
  └─ Phase 7: Final Logs           — Profiler flush + save trace
```

## 附录 C: 配置结构体依赖关系

```
RuntimeConfig
├── LogConfig
│   ├── 从 runtime.json 加载
│   └── 被 InitLogger() 消费（仅 Init 时）
├── FrameConfig
│   ├── 从 runtime.json 加载
│   └── 被 Engine::Init() 消费（仅 Init 时，生成 frame_interval_）
├── scripts_dir → ScriptVM::SetImportPath()
├── sandbox_level → ScriptVM 构造参数
└── physics_scene_path → PhysicsEngineBridge::Initialize()

ServerConfig
├── HttpConfig → net_http_bind.cc（每次 HTTP 请求读取）
├── MsgpackConfig → msgpack_bind.cc（每次消息解析读取）
├── admin_port → admin HTTP server 启动（仅 Init 时）
├── mongodb_dev / mongodb_public → MongoDB 连接
└── db_service → DatabaseService 配置加载

PhysicsConfig (独立管理器)
├── PhysicsConfig → 物理模拟参数（无热更）
├── ThreadingConfig → 线程模型参数（无热更）
├── PhysicsLogConfig → 物理日志（level 可热更）
└── ThresholdsConfig → 碰撞检测阈值（可热更）

MongoDbConfig (随 server.json 加载)
├── MongoDbConnectionConfig
├── MongoDbClusterInfo
├── MongoDbNodeConfig[]  (vector)
├── MongoDbSecurityConfig
├── MongoDbStorageConfig
├── MongoDbScriptsConfig
└── MongoDbDriversConfig

独立配置（不归 ConfigManager 管）
├── ProfilerConfig    → profiler_core.h（硬编码）
├── SpaceConfig       → space.h（Lua 注入）
├── ConnectorConfig   → connector.h（setter 注入）
└── DbServiceConfig   → db_service_config.h（ConfigManager 静态方法加载）
```
