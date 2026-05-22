# 游戏服务器日志系统技术报告

> 基于 Quill v11.1.0，面向 evpp 游戏服务器引擎

## 1. 游戏服务器日志需求分析

### 1.1 核心要求

| 维度 | 要求 | 说明 |
|------|------|------|
| 性能 | < 100ns 热路径延迟 | 日志不能成为帧循环瓶颈 |
| 异步 | 后台线程 IO | 格式化、写盘全部离主线程 |
| 分级 | ≥ 6 级（trace/debug/info/warn/error/fatal） | 按环境、模块动态控制 |
| 结构化 | 支持 JSON/CSV | 便于日志平台采集、检索、聚合 |
| 轮转 | 按大小/时间切分 | 防止磁盘写满 |
| 崩溃安全 | 信号处理 + 缓冲区清理 | crash 时尽量不丢日志 |
| 上下文 | 支持附加 trace_id / player_id / room_id | 链路追踪 |
| 跨平台 | Windows / Linux / macOS | 开发在 Windows，部署在 Linux |

### 1.2 游戏服务器特有场景

- **玩家链路追踪**：一个玩家请求横跨 gate → scene → db 多个进程，需要透传 trace_id
- **AOI 事件日志**：大量高频进出视野事件，需支持采样/限流
- **战斗回放**：关键操作记录到结构化日志供回放系统消费
- **GM 指令审计**：所有管理员操作强制记录，不可丢失
- **热更日志**：脚本热加载时需要清晰的 before/after diff 日志

---

## 2. Quill 架构分析

### 2.1 双阶段设计

```
┌─────────────────────────────────────────────────┐
│  调用线程 (Frontend)                              │
│  ┌──────────┐    binary encode    ┌────────────┐ │
│  │ LOG_INFO ├────────────────────→│ SPSC Queue │ │
│  └──────────┘   (无格式化)        └──────┬─────┘ │
└──────────────────────────────────────────┼───────┘
                                           │ lock-free
┌──────────────────────────────────────────┼───────┐
│  后台线程 (Backend)                       │       │
│  ┌──────────┐  decode → format → ┌───────▼─────┐ │
│  │ Sink 1..N│←────────────────────│ TransitEvent│ │
│  └──────────┘                     └─────────────┘ │
└─────────────────────────────────────────────────┘
```

**关键优势**：
- 热路径上不做任何格式化、字符串拷贝、内存分配
- 参数以二进制形式直接写入 lock-free SPSC 队列
- 时间戳取 x86 TSC（rdtsc 指令），开销 ~20 个 CPU 周期

### 2.2 队列模型

| 队列类型 | 行为 | 适用场景 |
|----------|------|----------|
| `UnboundedBlocking` | 无界、满时自旋等待 | 默认，审计日志 |
| `UnboundedDropping` | 无界、满时丢弃新消息 | 高频非关键日志 |
| `BoundedBlocking` | 固定大小、满时自旋 | 内存受限环境 |
| `BoundedDropping` | 固定大小、满时丢弃 | 极限性能场景 |

### 2.3 Sink 体系

| Sink | 用途 |
|------|------|
| `ConsoleSink` | 开发环境彩色输出 |
| `FileSink` | 普通文件输出 |
| `RotatingFileSink` | 按大小轮转 |
| `JsonSink` | 结构化 JSON 输出 |
| `RotatingJsonFileSink` | JSON + 轮转 |
| `NullSink` | 性能测试/禁用 |

### 2.4 宏系统

三种风格对应三种场景：

```cpp
// 传统 printf 风格 —— 通用
LOG_INFO(logger, "player {} entered room {}", player_id, room_id);

// 变量名风格 —— 调试
LOGV_INFO(logger, "state change", old_state, new_state);

// JSON 结构化 —— 数据管道
LOGJ_INFO(logger, "trade", "seller", seller_id, "buyer", buyer_id, "item", item_id);
```

### 2.5 跨平台支持

Quill 对三大平台一视同仁，API 完全一致：

| 特性 | Windows | Linux | macOS |
|------|---------|-------|-------|
| TSC 时钟 | `__rdtsc()` | `__builtin_ia32_rdtsc()` | 同 Linux（x86）/ 回退 systime（ARM） |
| 后台线程 | `std::thread` + Win32 命名 | `pthread_setname_np` | 同 Linux |
| 信号处理 | `SetUnhandledExceptionFilter` | `sigaction(SIGSEGV/SIGABRT)` | 同 Linux |
| 文件 IO | `fopen_s` / `_fileno` | `fopen` / `fileno` | 同 Linux |
| 大页内存 | 不支持 | `madvise(MADV_HUGEPAGE)` | 不支持 |
| ConsoleSink 颜色 | ANSI 转义序列（Win10+） | ANSI 转义序列 | 同 Linux |
| 构建 | CMake + MSVC / Clang-CL | CMake + GCC / Clang | 同 Linux |

> 注意：大页内存在 Windows/macOS 上自动回退到普通内存分配，无需用户处理。

---

## 3. 与 glog 对比

| 维度 | glog（当前使用） | quill（推荐） |
|------|-----------------|---------------|
| 日志模式 | 同步 | 异步（SPSC 队列） |
| 格式化时机 | 调用线程 | 后台线程 |
| 热路径开销 | 高（snprintf + write） | 低（二进制编码） |
| 结构化日志 | 不支持 | 原生 JSON |
| 自定义 Sink | 有限 | 完善接口 |
| 信号安全 | 基本 | 内置 crash handler |
| 频率控制 | 无 | 内置 rate limiting |
| 跨平台 | Linux / Windows / macOS | Windows / Linux / macOS |
| 编译期过滤 | GOOGLE_STRIP_LOG | QUILL_COMPILE_ACTIVE_LOG_LEVEL |

---

## 4. 引擎集成方案

### 4.0 文件结构

```
engine/core/log/
├── log.h              # 对外接口：GetLogger / InitLogger / ShutdownLogger
├── log_config.cc      # 实现
├── log_levels.h       # 级别定义
├── log_macros.h       # ENGINE_LOG_* 宏封装
├── log_context.h      # TraceContext（线程局部存储）
└── sinks/             # 自定义 Sink
    ├── metrics_sink.h
    └── remote_sink.h
```

### 4.1 日志层级定义

```cpp
// engine/core/log/levels.h
// Quill 默认 9 级 → 游戏简化为 6 级
namespace engine {

using LogLevel = quill::LogLevel;

// Quill 原生级别 → 游戏语义
// TraceL3/TraceL2/TraceL1  → 内部使用，不暴露
// Debug                     → DEBUG   开发调试
// Info                      → INFO    关键业务流程
// Warning                   → WARN    可恢复异常
// Error                     → ERROR   不可恢复，当前操作失败
// Critical                  → FATAL   进程即将退出

constexpr auto kDefaultLevel = LogLevel::Info;

} // namespace engine
```

### 4.2 推荐配置

```cpp
// engine/core/log/config.h

inline quill::FrontendOptions GetFrontendOptions() {
    quill::FrontendOptions opts;
    opts.queue_type = quill::QueueType::UnboundedBlocking;
    opts.initial_queue_capacity = 256 * 1024;  // 256 KiB
    return opts;
}

inline quill::BackendOptions GetBackendOptions() {
    quill::BackendOptions opts;
    opts.sleep_duration = std::chrono::microseconds{500};
    opts.transit_events_soft_limit = 16384;
    opts.sink_min_flush_interval = std::chrono::milliseconds{100};
    return opts;
}
```

### 4.3 Logger 分层策略

```
引擎 Logger 树：

root (INFO)
├── engine (INFO)
│   ├── engine.network (DEBUG)     # 网络收发
│   ├── engine.scene (INFO)        # 场景/AOI
│   ├── engine.combat (INFO)       # 战斗
│   ├── engine.db (WARN)           # 数据库
│   └── engine.gm (INFO)           # GM 指令 —— 强制 flush
├── gate (INFO)
│   ├── gate.session (INFO)
│   └── gate.protocol (DEBUG)
└── scene (INFO)
    ├── scene.aoi (WARN)           # AOI 高频，默认 WARN
    └── scene.movement (INFO)
```

每个 logger 可绑定不同 Sink 组合：
- `engine.gm` → FileSink（不可丢失） + ConsoleSink
- `engine.network` → RotatingFileSink（高频、轮转）
- `engine.combat` → RotatingJsonFileSink（战斗回放）

### 4.4 上下文传播

```cpp
// 利用 quill 的 Logger::set_logger_details() 注入自定义字段
// 或使用 LOGJ_ 风格附加结构化字段

#define ENGINE_LOG_INFO(logger, fmt, ...)                              \
    LOG_INFO(logger, "[{}][{}][{}] " fmt,                             \
             TraceContext::trace_id(),                                 \
             TraceContext::player_id(),                                \
             TraceContext::room_id(),                                  \
             ##__VA_ARGS__)
```

### 4.5 独立初始化（与 evpp 解耦）

日志模块代码位于 `engine/core/log/`。作为基础设施，在 evpp 事件循环之前独立初始化，在事件循环停止之后独立关闭。不与 evpp 任何组件耦合。

```cpp
// engine/core/log/log_config.cc

namespace engine {

void InitLogger(const std::string& log_dir) {
    // 1. 设置日志目录
    std::string log_path = log_dir.empty() ? "logs" : log_dir;

    // 2. 启动后台日志线程（独立于 evpp 事件循环）
    quill::Backend::start(GetBackendOptions());

    // 3. 创建 sink
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
        "console");

    quill::RotatingFileSinkConfig file_cfg;
    file_cfg.set_rotation_max_file_size(100 * 1024 * 1024);  // 100MB
    file_cfg.set_max_backup_files(10);
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        log_path + "/engine.log", file_cfg);

    // 4. 创建 root logger（绑定所有 sink）
    quill::Frontend::create_or_get_logger(
        "root", {console_sink, file_sink},
        quill::PatternFormatterOptions{
            "%(time) [%(short_log_level)] [%(logger)] %(message)"});
}

void ShutdownLogger() {
    // 日志关闭在 evpp 事件循环完全停止之后调用
    // Backend::stop() 阻塞排空所有 SPSC 队列，确保日志不丢
    quill::Backend::stop();
}

} // namespace engine
```

```cpp
// main.cc —— 启动顺序
int main() {
    engine::InitLogger("logs");       // 1. 日志最先初始化
    evpp::EventLoop loop;             // 2. 然后初始化 evpp
    // ...
    loop.Stop();                      // 3. 先停 evpp
    engine::ShutdownLogger();         // 4. 最后停日志
    return 0;
}
```

> 核心原则：日志系统不依赖 evpp，evpp 线程可以安全使用日志（每线程独立 SPSC 队列），两者仅通过调用顺序耦合——日志先启后停。

---

## 5. 最佳实践

### 5.1 性能优化

1. **编译期裁剪**：Release 构建设置 `QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO`，移除 TRACE/DEBUG 调用
2. **高频路径用限流**：AOI、移动同步等日志用 `LOG_DEBUG_LIMIT(1s, ...)` 限制频率
3. **队列选型**：GM 审计用 UnboundedBlocking 不丢数据，AOI 事件用 UnboundedDropping 防止内存暴涨
4. **时钟源**：保留默认 TSC，无需额外配置

### 5.2 运维

1. **JSON 日志接入 ELK/Loki**：RotatingJsonFileSink 输出 → Filebeat 采集
2. **按模块分文件**：gate.log / scene.log / combat.log / gm.log / error.log
3. **轮转策略**：单文件 100MB + 保留 10 个历史文件
4. **Crash 保护**：`quill::BackendOptions::error_notifier` 设置回调，crash 时 flush 所有 buffer

### 5.3 禁则

- 不要在热路径上使用 `LOGJ_`（JSON 格式化开销高于普通）
- 不要在日志消息中传大对象（如完整 protobuf/glaze 序列化结果），日志只记关键 ID
- 不要把日志系统初始化放在静态初始化阶段（顺序不确定）
- 不要在生产环境长期开启 DEBUG 级别

---

## 6. 迁移路径（从 glog）

```
阶段 1（并行）：
  quill 与 glog 共存，新模块用 quill，老模块保持 glog

阶段 2（适配）：
  封装 ENGINE_LOG_* 宏，底层切换 quill
  #define LOG_INFO  →  ENGINE_LOG_INFO   （全局替换）

阶段 3（移除）：
  删除 glog 依赖，清理 logging.h 中 glog 代码
```

---

## 7. 性能基准

### 7.1 Quill 官方数据（x86-64, Linux, gcc-12）

| 操作 | 延迟 | 说明 |
|------|------|------|
| LOG_INFO 热路径 | ~7 ns | 仅编码 + 入队，无格式化 |
| LOG_INFO + 格式化 | 后台线程 100-500 ns | 不在调用路径 |
| Backend 吞吐 | ~10M msg/s | 单线程处理 |
| SPSC 入队 | ~4 ns | lock-free, 2-3 条 mov 指令 |

### 7.2 游戏场景预估

假设单进程 16 个 EventLoop 线程，每帧 33ms（30 tick）：

| 日志级别 | 每条耗时 | 每帧 100 条 | 每帧 1000 条 |
|----------|---------|------------|-------------|
| TRACE/DEBUG | ~7 ns | 0.7 us | 7 us |
| INFO | ~7 ns | 0.7 us | 7 us |
| INFO + LOGJ | ~15 ns | 1.5 us | 15 us |

> 结论：即使每帧 1000 条日志，热路径总开销 < 0.1%，对帧率无影响。

### 7.3 磁盘 IO 预估

| 日志量 | 单条大小 | 每小时 | 每天 |
|--------|---------|--------|------|
| 中等（100条/帧/进程） | 150B | ~1.6 GB | ~38 GB |
| 高压（500条/帧/进程） | 150B | ~8 GB | ~192 GB |

> 按 100MB 轮转 + 保留 10 个文件 = 每进程 ~1GB 磁盘占用。

---

## 8. 日志格式规范

### 8.1 文本格式（开发环境 / ConsoleSink）

```
[2026-05-22 14:30:01.123456] [INFO] [engine.scene] [tid:12345] [trace:abc123] [player:10001] [room:5] player entered room
│                         │      │              │          │                │              │        └─ 消息
│                         │      │              │          │                │              └─ room_id
│                         │      │              │          │                └─ player_id
│                         │      │              │          └─ trace_id
│                         │      │              └─ 线程ID
│                         │      └─ Logger 名
│                         └─ 级别
└─ 时间戳（微秒精度）
```

对应 PatternFormatter：
```
%(time) [%(short_log_level)] [%(logger)] [tid:%(thread_id)] %(message)
```
> 自定义字段（trace_id / player_id / room_id）通过 ENGINE_LOG_* 宏注入到 %(message) 中，非 PatternFormatter 原生支持。

### 8.2 JSON 格式（生产环境 / JsonSink）

```json
{
  "timestamp": "2026-05-22T14:30:01.123456Z",
  "level": "INFO",
  "logger": "engine.scene",
  "thread_id": 12345,
  "trace_id": "abc123",
  "player_id": 10001,
  "room_id": 5,
  "message": "player entered room",
  "source": {
    "file": "scene_service.cc",
    "line": 142,
    "function": "OnPlayerEnterRoom"
  }
}
```

### 8.3 自定义字段注入

方式一：通过宏注入到消息体（参见 4.4），适用于文本格式。

```cpp
#define ENGINE_LOG_INFO(logger, fmt, ...)                              \
    LOG_INFO(logger, "[{}][{}][{}] " fmt,                             \
             TraceContext::trace_id(),                                 \
             TraceContext::player_id(),                                \
             TraceContext::room_id(),                                  \
             ##__VA_ARGS__)
```

方式二：通过 LOGJ_ 注入为 JSON 字段，适用于结构化格式。

```cpp
#define ENGINE_LOGJ_INFO(logger, msg, ...)                             \
    LOGJ_INFO(logger, msg,                                             \
              "trace_id", TraceContext::trace_id(),                    \
              "player_id", TraceContext::player_id(),                  \
              "room_id", TraceContext::room_id(),                      \
              ##__VA_ARGS__)
```

---

## 9. 线程安全

### 9.1 模型

```
线程1 (EventLoop) ──→ SPSC Queue 1 ──┐
线程2 (EventLoop) ──→ SPSC Queue 2 ──┤
线程3 (EventLoop) ──→ SPSC Queue 3 ──┼──→ Backend Thread ──→ Sinks
    ...                               │
线程N (EventLoop) ──→ SPSC Queue N ──┘
```

- **每个调用线程独立 SPSC 队列**：无锁竞争
- **Backend 独占读取**：单线程消费，无需锁
- **Sink 写入**：Backend 线程串行调用，无并发问题

### 9.2 关键约束

| 场景 | 约束 |
|------|------|
| Logger 创建 | 线程安全（内部 spinlock） |
| 日志写入 | 无锁（每线程独立队列） |
| Backend 启动 | `std::call_once` 保证单次 |
| 信号处理 | 内置 `SignalHandler`，crash 时 flush |
| Shutdown | `Backend::stop()` 阻塞排空所有队列 |

### 9.3 跨进程日志

```
Gate → Scene → DB 请求链路：

  [Gate]                           [Scene]                         [DB]
  trace_id = uuid()  ──RPC──→  trace_id = 透传  ──RPC──→  trace_id = 透传
  LOG(..., trace_id)            LOG(..., trace_id)           LOG(..., trace_id)
        │                             │                              │
        └─────────────────────────────┴──────────────────────────────┘
                                          │
                                  ELK 中按 trace_id 聚合
                                  完整还原请求链路
```

实现方式：
```cpp
// evpp TCP connection 携带 trace context
// 从 HTTP header / 自定义协议头 解析并注入到线程局部存储

class TraceContext {
    thread_local static inline std::string trace_id_;
    thread_local static inline uint64_t player_id_;
    thread_local static inline uint64_t room_id_;
public:
    static void set_trace_id(std::string id) { trace_id_ = std::move(id); }
    static const std::string& trace_id() { return trace_id_; }
    // ...
};
```

---

## 10. 故障处理

### 10.1 队列满

| 队列类型 | 行为 | 监控指标 |
|----------|------|----------|
| Blocking | 自旋等待（800ns 重试间隔） | 自旋计数 → 告警 |
| Dropping | 丢弃新消息 + 计数 | dropped_count → 告警 |

### 10.2 磁盘满

```cpp
// 自定义 Sink error handler
quill::BackendOptions opts;
opts.error_notifier = [](std::string_view error_msg) {
    // 写 stderr 兜底 + 更新 Prometheus 指标
    std::cerr << "[LOG_ERROR] " << error_msg << std::endl;
    Metrics::log_errors.Increment();
};
```

### 10.3 进程崩溃

quill 内置 SignalHandler，自动 flush 所有 SPSC 队列到 Sink：
- Linux/macOS：捕获 `SIGSEGV`、`SIGABRT`
- Windows：通过 `SetUnhandledExceptionFilter` 捕获 SEH 异常

```cpp
quill::BackendOptions opts;
opts.enable_signal_handler = true;  // 默认开启，无需额外代码
```

---

## 11. 开发工作流

### 11.1 本地开发

```cpp
// engine/core/log/log_config.cc

void InitLogger(const std::string& log_dir) {
    quill::Backend::start(GetBackendOptions());

#ifdef NDEBUG
    // Release: 只写文件
    quill::RotatingFileSinkConfig file_cfg;
    file_cfg.set_rotation_max_file_size(100 * 1024 * 1024);
    file_cfg.set_max_backup_files(10);
    auto sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        log_dir + "/engine.log", file_cfg);
#else
    // Debug: 彩色控制台（直观）
    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
#endif

    quill::Frontend::create_or_get_logger("root", {sink});
}
```

### 11.2 日志级别热切换

```cpp
// 通过 GM 指令或 HTTP 接口动态调整
void SetLogLevel(const std::string& logger_name, const std::string& level) {
    auto* logger = quill::Frontend::get_logger(logger_name);
    auto* root = quill::Frontend::get_logger("root");
    if (logger) {
        logger->set_log_level(ParseLevel(level));
        LOG_INFO(root, "log level changed: {} -> {}", logger_name, level);
    }
}

// GM 指令示例：
//   /gm loglevel engine.scene debug    → 临时开启场景 DEBUG
//   /gm loglevel engine.aoi error      → 降低 AOI 日志级别
```

### 11.3 单元测试中日志隔离

```cpp
// 测试用例中重定向日志到 NullSink，避免刷屏
class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto null_sink = quill::Frontend::create_or_get_sink<quill::NullSink>("null");
        test_logger_ = quill::Frontend::create_or_get_logger(
            "test", null_sink, quill::PatternFormatterOptions{});
    }
    quill::Logger* test_logger_;
};
```

---

## 12. Sink 定制示例

### 12.1 指标 Sink（Prometheus）

```cpp
// 自定义 Sink：统计各级别日志数量，暴露为 Prometheus 指标
class MetricsSink : public quill::Sink {
public:
    void write_log(quill::MacroMetadata const* metadata,
                   uint64_t /* timestamp */,
                   std::string_view /* thread_id */,
                   std::string_view /* thread_name */,
                   std::string_view /* logger_name */,
                   quill::LogLevel level,
                   std::string_view /* message */) override
    {
        level_counters_[static_cast<int>(level)].fetch_add(1);
    }

    void flush_sink() override {}

    uint64_t error_count() const {
        return level_counters_[static_cast<int>(quill::LogLevel::Error)].load();
    }
    uint64_t warn_count() const {
        return level_counters_[static_cast<int>(quill::LogLevel::Warning)].load();
    }

private:
    std::array<std::atomic<uint64_t>, 10> level_counters_{};
};
```

### 12.2 网络 Sink（远程日志）

```cpp
// 通过独立 socket 将日志发送到远程日志服务
// 注意：不依赖 evpp，使用原生 socket + 非阻塞 IO
class RemoteSink : public quill::Sink {
    int fd_;
    std::string remote_addr_;
public:
    explicit RemoteSink(const std::string& addr) : remote_addr_(addr) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        // 设置非阻塞 + 连接远程日志服务
        evutil_make_socket_nonblocking(fd_);
        // ... connect to remote_addr_ ...
    }

    ~RemoteSink() override {
        if (fd_ >= 0) { EVUTIL_CLOSESOCKET(fd_); }
    }

    void write_log(quill::MacroMetadata const* metadata,
                   uint64_t, std::string_view, std::string_view,
                   std::string_view, quill::LogLevel,
                   std::string_view formatted_message) override
    {
        // 非阻塞 send，失败不重试（日志系统不阻塞）
        ::send(fd_, formatted_message.data(), formatted_message.size(), 0);
    }

    void flush_sink() override {
        // 非阻塞：不等待远端确认
    }
};
```

---

## 13. 日志轮转策略

### 13.1 推荐配置

```cpp
quill::RotatingFileSinkConfig rotation_cfg;
rotation_cfg.set_rotation_max_file_size(100 * 1024 * 1024);  // 100MB
rotation_cfg.set_max_backup_files(10);                        // 保留 10 个历史
rotation_cfg.set_rotation_naming_scheme(
    quill::RotatingFileSinkConfig::RotationNamingScheme::Index);
rotation_cfg.set_remove_old_files(true);                      // 自动删除最旧
rotation_cfg.set_rotation_frequency(
    quill::RotatingFileSinkConfig::RotationFrequency::Daily); // 每天零点也切一次
```

### 13.2 文件命名

```
logs/
├── gate.log              # 当前写入
├── gate.log.1            # 最近轮转
├── gate.log.2
├── ...
├── gate.log.10           # 最旧（即将被删除）
├── scene.log
├── combat.json           # JSON 格式
├── gm.log                # GM 审计日志（永不轮转？视需求）
└── error.log             # 仅 ERROR 及以上
```

### 13.3 按级别的文件分流

```cpp
// 全局 error.log 只收 ERROR + FATAL
auto error_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
    "logs/error.log", rotation_cfg);
error_sink->set_log_level(quill::LogLevel::Error);  // Sink 级别过滤

// 业务日志收 INFO 及以上
auto app_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
    "logs/app.log", rotation_cfg);
```

---

## 14. 风险分析与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| Backend 线程崩溃 | 日志全部丢失 | 低 | `error_notifier` + 监控 Backend 线程存活 |
| SPSC 队列内存暴涨 | OOM | 中 | UnboundedDropping 限制 + 内存水位告警 |
| 磁盘 IO 瓶颈 | 日志堆积、延迟 | 中 | 独立物理盘 + 监控 flush 延迟 |
| 格式化失败（类型不匹配） | 编译通过但运行时输出乱码 | 低 | Codec 编译期检查 + 默认回退 "?" |
| SPSC 队列存数据时 crash | 未 flush 日志丢失 | 低 | SignalHandler 自动 flush |
| 跨 DLL 使用 logger | ABI 不兼容 | 低 | 只在 .exe 中持有 logger 指针 |

---

## 15. 详细迁移步骤

### 阶段 1：共存（1-2 天）

```cpp
// 新增 engine/core/log/log.h，封装 quill
#pragma once
#include <quill/Quill.h>

namespace engine {

// 获取 engine 模块默认 logger
quill::Logger* GetLogger(const std::string& name = "engine");

// 初始化（在 main 中调用）
void InitLogger(const std::string& log_dir);

// 关闭（在 main 末尾调用）
void ShutdownLogger();

} // namespace engine
```

老代码保持 `LOG_INFO << ...`，新模块使用 `ENGINE_LOG_INFO(...)`。

### 阶段 2：宏适配（1 周）

```cpp
// engine/core/log/macros.h
// 统一日志宏，全局替换 #include "evpp/logging.h" → #include "engine/core/log/macros.h"

#pragma once
#include <quill/Quill.h>

#define ENGINE_LOG_TRACE(logger, fmt, ...)   LOG_TRACE_L1(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_DEBUG(logger, fmt, ...)   LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_INFO(logger, fmt, ...)    LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_WARN(logger, fmt, ...)    LOG_WARNING(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_ERROR(logger, fmt, ...)   LOG_ERROR(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_FATAL(logger, fmt, ...)   LOG_CRITICAL(logger, fmt, ##__VA_ARGS__)

// 限流版本
#define ENGINE_LOG_DEBUG_LIMIT(d, logger, fmt, ...) \
    LOG_DEBUG_LIMIT(d, logger, fmt, ##__VA_ARGS__)
```

### 阶段 3：移除 glog（1 天）

```bash
# 移除依赖
git rm evpp/logging.h          # glog wrapper 不再需要
# 修改 CMakeLists.txt 移除 glog find_library
# 修改 vcpkg.json 移除 glog dependency
```

---

## 16. 决策总结

| 决定 | 选择 | 理由 |
|------|------|------|
| 日志库 | Quill v11.x | 异步无锁、原生 JSON、现代 C++17 |
| 默认队列 | UnboundedBlocking | 不丢日志，性能足够 |
| 文本格式 | PatternFormatter 自定义 | 统一团队可读格式 |
| 生产格式 | JSON | ELK/Loki 直接索引 |
| 跨平台 | Windows / Linux / macOS | 单套代码，三平台一致 |
| 构建方式 | 源码编译（submodule） | 便于定制、调试、打补丁 |
| 编译期最低级别 | Release: INFO, Debug: TRACE | 平衡性能与可观测性 |
| 时钟源 | TSC（默认） | 最低开销 |
| Sink 线程 | 跟随 Backend 单线程 | 无锁设计，写入安全 |

---

*报告版本: v4*  
*日期: 2026-05-22*
