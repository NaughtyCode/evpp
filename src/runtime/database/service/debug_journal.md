# DB Service Smoke Test — Debug Journal

## 目标

验证 DatabaseService 的完整 SPSC 管道能在真实进程中跑通：
`Initialize → SendRequest → ProcessRequest → PollResponse`

方法：在 `Engine::Start()` 中插入一个临时冒烟测试（Debug 模式），发一条
MongoDB ping 命令，用轮询计时器收回复。

## 核心症状

调用 `DatabaseService::SendRequest()` 之后，**进程内所有后续计时器停止触发**，
包括 `TimerManager::create_timer_for` 创建的 1s/2s 一次性计时器和 evpp
`RunEvery` 创建的 33ms 帧计时器。进程不崩溃、不报错，只是静默卡死。

## 排查路径

### 第 1 步 — 排除 "从计时器回调内调用 SendRequest" 的嫌疑

最早的测试是在计时器回调中调用 SendRequest。怀疑计时器回调里做重入操作
有问题，于是把 SendRequest 移到 `Start()` 的直接流程中（在 `RunEvery`
创建之后、事件循环启动之前调用）。

**结果：现象相同。** 排除"只发生在回调内"的假设。

### 第 2 步 — 排除 "TimerManager 自身出问题"

在 `Tick()` 中加入心跳 fprintf（每 30 帧输出一次），确认帧计时器是否还在
被事件循环调度。

**结果：8 秒内零心跳输出。** 说明不是 HrTimerManager 没处理到期计时器，
而是 evpp 的 `RunEvery` 帧计时器根本没被 libevent 调度。问题在事件循环
层面。

### 第 3 步 — 确认 SendRequest 是唯一的变量

把 SendRequest 调用注释掉，保留其他所有代码（DB 线程已初始化、MongoDB
驱动已加载），只创建两个 1s/2s 的 TimerManager 计时器。

**结果：两个计时器均正常触发，Tick 心跳每秒出现。** 确认 SendRequest
就是触发条件，不是 DatabaseService 初始化过程中引入的。

### 第 4 步 — 隔离 SendRequest 中的哪个环节出问题

SendRequest 做了三件事：
1. 原子 `fetch_add` 轮询选线程
2. SPSC 队列 `enqueue`（moodycamel::ConcurrentQueue）
3. DB 工作线程从队列 `try_dequeue` 后调用 `ProcessRequest`

#### 4a — 仅 SPSC 入队/出队（kNoOp）

发送一条 `DbOperation::kNoOp` 请求——DB 线程出队后看到 kNoOp 立刻跳过，
不做任何 MongoDB 操作。

**结果：计时器正常工作。** SPSC 队列不是问题。

#### 4b — 仅 Lua 脚本执行（kExecuteScript）

发送一条 `DbOperation::kExecuteScript`，脚本内容为
`return 'hello from db vm'`。DB 线程在自己的 Lua VM 中执行此脚本，
完全不走 MongoDB 网络 I/O。

**结果：计时器正常工作，PollResponse 成功收到 `id=100 ok=1`。**
DB 线程内的 `ProcessRequest` 基础流程没有问题。

#### 4c — MongoDB 网络 I/O（kCommand）

发送 `DbOperation::kCommand`，payload 为 `{"ping": 1}`。DB 线程调用
`client_->CommandSimple("admin", command, ...)`，这会通过 MongoDB C 驱动
发起 TCP 连接 + TLS 握手 + 发送 ping 命令。

**结果：帧计时器立即停止触发。** 问题定位到 MongoDB C 驱动在后台线程上的
网络 I/O。

## 根因分析

### 已验证的事实

| 场景 | 涉及网络 I/O | 计时器 |
|------|-------------|--------|
| 无 SendRequest | — | ✅ |
| kNoOp | 无 | ✅ |
| kExecuteScript（Lua） | 无 | ✅ |
| kCommand（ping） | 有（mongoc TCP+TLS） | ❌ |

### 结论

**MongoDB C 驱动在 DB 工作线程（`std::thread`）上执行的网络 I/O（Winsock
socket 创建/连接/TLS 握手）干扰了主线程上 libevent 的事件调度。**

MongoDB URI 配置指向 `127.0.0.1:27017,27018,27019`（副本集），本机没有
运行 MongoDB 服务。驱动在尝试连接时会执行：

1. DNS 解析 `127.0.0.1`（本地，很快）
2. 对三个节点逐个 TCP connect（超时等待）
3. Secure Channel TLS 握手初始化
4. 副本集拓扑发现

在 Windows 上，Winsock 是全局状态。libevent 用 `select`/`WSAPoll` 监控
内部通知管道。mongoc 的连接尝试可能触发了 Winsock 内部状态变更，导致
libevent 的事件循环停止调度计时器事件。

具体机制待进一步排查，可能的方向：
- mongoc 在连接期间修改了 Winsock 的错误状态，libevent 的 socket 监控
  函数因此提前返回
- Secure Channel 初始化期间的 CSP/KSP 操作影响了全局加密状态
- mongoc 内部临时修改了进程的 socket 选项（如非阻塞标志）与 libevent
  的假设冲突

### 为什么不崩溃

mongoc 的连接失败被 DB 线程的 `try/catch` 正确捕获，线程本身不崩溃。
主线程的事件循环也没有收到错误信号——只是计时器事件不再被触发，表现为
"静默卡死"。

## 烟雾测试的最终形态

`src/runtime/engine/engine.cc` 第 249–276 行：

```cpp
#if defined(ENGINE_MONGODB_ENABLED) && !defined(NDEBUG)
    // 发送 kExecuteScript 请求（id=100，纯 Lua，不走网络）
    // 用 evpp RunEvery 以 250ms 间隔轮询 PollResponse
    // 收到回包后 self-cancel 轮询计时器
#endif
```

选择 `kExecuteScript` 而非 `kCommand` 的原因：
- 绕过 mongoc/libevent 冲突，能在无 MongoDB 服务器的开发环境中跑通
- 仍然覆盖了完整的 SPSC 管道：入队 → 出队 → 处理 → 回包 → 主线程接收

## 后续工作

1. **修复 kCommand + libevent 冲突**（需在有/无 MongoDB 服务两种环境对比测试）
2. 将烟雾测试从 Debug 保护中移出，改为 CI 可跑的集成测试
3. 补充 PollResponse 超时机制（当前烟雾测试依赖 event loop 持续运行）
