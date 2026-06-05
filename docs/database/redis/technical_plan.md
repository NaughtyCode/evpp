# Redis 模块技术方案

> 来源：基于 `docs/database/redis/draft.txt` 的要求，并结合当前工程中 `data_service`、`physics`、`vm`、`config`、`thirdparty`、`CMake` 的现状整理。

## 1. 目标

在 server 端集成 hiredis，并在 `src/runtime/database/redis` 下实现一个线程安全的 Redis 访问模块。

核心目标：

- 只有 server 启用 Redis 模块，client 和 mobile 不启用。
- hiredis 和 libevent 全部运行在独立线程 `RedisClientThread` 中。
- Redis 模块对外只暴露全局单例 `RedisClient`。
- 任意线程都可以通过 `RedisClient` 发起 Redis 请求。
- 请求通过 `moodycamel::ConcurrentQueue` 传递给 `RedisClientThread`。
- Redis 请求成功、失败、连接错误、超时、关闭丢弃等情况都要回调调用方。
- 每个线程自己的 Lua VM 都可以导出 Redis API。
- Redis 结果不能在 Redis 线程直接访问调用方 Lua VM，必须先进入调用方所属线程的结果队列，再由调用方线程主循环 dispatch。
- `RedisClientScriptVM` 是 Redis 线程私有 Lua VM，外部不可访问。
- `RedisClientScriptVM` 通过 `custom_ptr_store` 访问 Redis 模块级实例。
- 外部访问 Redis 的跨线程结果派发机制要实现为公共基础设施，便于后续复用。

## 2. 当前工程现状

### 2.1 已具备的第三方依赖

当前工程已经存在以下依赖：

- `src/thirdparty/hiredis`
- `src/thirdparty/hiredis/adapters/libevent.h`
- `src/thirdparty/libevent`
- `src/thirdparty/concurrentqueue/concurrentqueue`

server 端 CMake 已经集成并链接 libevent，concurrentqueue 也已经在 include path 中。hiredis 目前存在源码，但还没有接入 server target。

### 2.2 可复用的工程模式

现有 `src/runtime/database/data_service` 可以作为 Redis 模块的主要参照：

- `DatabaseService` 是公共单例。
- `DBThread` 是内部线程。
- `DBScriptVM` 是线程私有 VM。
- 使用 `moodycamel::ConcurrentQueue` 传递请求和响应。
- 通过 `module_access.h` 限制内部类访问。
- 通过 `custom_ptr_store` 把 C++ 对象挂入 Lua VM。

现有 `src/runtime/physics` 也有可参考点：

- 使用独立线程。
- 使用条件变量或唤醒机制降低请求延迟。
- 私有 `PhysicsScriptVM` 通过 `custom_ptr_store` 访问模块对象。

现有 `src/runtime/space/space_message` 和 `src/runtime/network/bind/net_lifetime.h` 对跨线程消息派发、Lua 生命周期保护有参考价值。

### 2.3 需要特别注意的问题

server 和 client 的 CMake option 有 cache 泄漏风险。当前根 CMake 先添加 server 再添加 client，Redis 的 server-only 不能只依赖全局 option，需要在 client/mobile target 显式禁用或使用 target-specific 判断。

另外，现有 `MainThreadScriptVM` 对 Mongo/DB binding 的宏判断存在组合宏使用，Redis 模块应避免重复这种隐式判断，建议使用清晰的 `ENGINE_REDIS_ENABLED` 和 server-only target 条件。

## 3. 总体架构

```text
调用线程 / 调用方 Lua VM
  -> Redis Lua binding 或 C++ RedisClient API
  -> RedisClient 全局单例，线程安全
  -> moodycamel::ConcurrentQueue<RedisRequest>
  -> RedisClientThread 独立线程
       - event_base
       - redisAsyncContext
       - RedisClientScriptVM
       - hiredis libevent adapter
  -> hiredis async callback
  -> RedisResult
  -> 调用方所属线程的 AsyncResultDispatcher
  -> 调用方线程主循环 dispatch
  -> C++ 回调 / Lua 回调
```

架构约束：

- `RedisClientThread` 是唯一 Redis IO 线程。
- `redisAsyncContext` 只在 `RedisClientThread` 中访问。
- hiredis 回调只在 `RedisClientThread` 中执行。
- hiredis 回调不能直接调用外部 Lua VM。
- 外部 Lua 回调必须回到它所属 VM 的线程执行。
- `RedisClient` API 必须线程安全。
- `RedisClientScriptVM` 外部不可访问。

## 4. 建议文件结构

```text
src/runtime/database/redis/
  redis_client.h
  redis_client.cc
  redis_client_thread.h
  redis_client_thread.cc
  redis_client_script_vm.h
  redis_client_script_vm.cc
  redis_request.h
  redis_result.h
  redis_value.h
  redis_client_config.h
  redis_client_config.cc
  redis_connection.h
  redis_connection.cc
  module_access.h
  bind/
    redis_bind.h
    redis_bind.cc

src/runtime/core/async/
  async_result_dispatcher.h
  async_result_dispatcher.cc

resources/config/server/
  redis.json

resources/script/redis/
  init.lua
```

如果工程不希望新增 `runtime/core/async`，也可以放在 `src/runtime/script/async_result_dispatcher.*`。但从复用性看，更推荐独立公共目录。

## 5. 公共 API 设计

### 5.1 RedisClient

`RedisClient` 是 Redis 模块唯一对外可见入口。

建议接口：

```cpp
class RedisClient {
 public:
  static RedisClient& Instance();

  bool Initialize(const RedisClientConfig& config);
  void Shutdown();

  bool IsRunning() const;
  bool IsHealthy() const;
  RedisClientStats GetStats() const;

  uint64_t NextRequestId();

  bool Command(
      std::vector<std::string> argv,
      RedisResultCallback callback,
      RedisCommandOptions options = {});

  bool Eval(
      std::string script,
      std::vector<std::string> keys,
      std::vector<std::string> args,
      RedisResultCallback callback,
      RedisCommandOptions options = {});
};
```

要求：

- `Initialize` 只能成功执行一次。
- `Shutdown` 可重复调用，必须幂等。
- `Command` 和 `Eval` 可由任意线程调用。
- 请求 accepted 后，必须保证最终回调一次。
- 如果队列满、模块未启动、正在关闭，应立即返回失败，或同步生成失败回调。具体策略建议保持一致：未入队返回 `false`；已入队必回调。
- callback 必须按草稿要求是线程安全的。Lua binding 层传入的 callback 不直接操作 Lua，只负责投递到 dispatcher。

### 5.2 RedisRequest

建议字段：

```cpp
struct RedisRequest {
  uint64_t request_id = 0;
  std::vector<std::string> argv;
  RedisCommandOptions options;
  RedisResultCallback callback;
  std::weak_ptr<AsyncResultDispatcher> caller_dispatcher;
  std::string trace_tag;
};
```

`argv` 使用 `std::string` 保存参数，hiredis 调用时使用 `redisAsyncCommandArgv`，保证二进制安全。

### 5.3 RedisResult

建议字段：

```cpp
enum class RedisResultStatus {
  kOk,
  kCommandError,
  kConnectionError,
  kTimeout,
  kQueueFull,
  kShutdown,
  kDropped
};

struct RedisResult {
  uint64_t request_id = 0;
  bool success = false;
  RedisResultStatus status = RedisResultStatus::kDropped;
  std::string error;
  RedisValue value;
  uint64_t elapsed_ms = 0;
};
```

### 5.4 RedisValue

`RedisValue` 要覆盖 hiredis reply 类型：

- nil
- string
- status
- error
- integer
- array

为后续 RESP3 预留扩展：

- double
- bool
- map
- set

Lua binding 中统一转换成 table：

```lua
{
  request_id = 1,
  success = true,
  status = "ok",
  error = nil,
  value = "...",
  value_type = "string",
  elapsed_ms = 3
}
```

## 6. RedisClientThread 设计

`RedisClientThread` 是 Redis 模块内部线程类，外部不可直接访问。

内部成员建议：

```cpp
class RedisClientThread {
 public:
  bool Start(const RedisClientConfig& config);
  void Stop();
  bool Enqueue(RedisRequest request);
  bool IsHealthy() const;

 private:
  void ThreadMain();
  void Connect();
  void Disconnect();
  void DrainRequests();
  void CompleteRequest(uint64_t request_id, RedisResult result);
};
```

线程内持有：

- `std::thread thread_`
- `event_base* event_base_`
- `redisAsyncContext* redis_context_`
- `RedisClientScriptVM script_vm_`
- `moodycamel::ConcurrentQueue<RedisRequest> request_queue_`
- `std::unordered_map<uint64_t, PendingRedisRequest> pending_requests_`
- wakeup event
- logger
- stats
- atomic running/stopping/healthy state

### 6.1 启动流程

1. `RedisClient::Initialize` 创建 `RedisClientThread`。
2. `RedisClientThread::Start` 创建独立线程。
3. 线程名设置为 `RedisClientThread`。
4. 线程内创建 `event_base`。
5. 初始化 `RedisClientScriptVM`。
6. 加载 Redis 线程脚本目录。
7. 创建 `redisAsyncContext`。
8. 调用 `redisLibeventAttach(redis_context_, event_base_)`。
9. 设置 connect callback 和 disconnect callback。
10. 注册 wakeup event。
11. 进入 `event_base_dispatch`。

### 6.2 请求流程

1. 调用方线程调用 `RedisClient::Command`。
2. `RedisClient` 生成 `request_id`。
3. 请求入 `request_queue_`。
4. 唤醒 Redis 线程。
5. Redis 线程 drain 队列。
6. 对每个请求调用 `redisAsyncCommandArgv`。
7. 请求进入 `pending_requests_`。
8. hiredis 回调触发。
9. hiredis reply 转换成 `RedisResult`。
10. 从 `pending_requests_` 删除请求。
11. 调用 request callback。
12. callback 把结果投递给调用方 dispatcher。

### 6.3 关闭流程

1. 设置 stopping。
2. 拒绝新请求。
3. 唤醒 Redis 线程。
4. 对 request queue 中尚未发送的请求生成 shutdown 失败结果。
5. 对 pending requests 生成 shutdown 或 connection error 失败结果。
6. 调用 `redisAsyncDisconnect`。
7. `event_base_loopbreak`。
8. join 线程。
9. 释放 `redisAsyncContext` 和 `event_base`。

关闭必须保证：

- 不遗留 pending callback。
- 不访问已销毁 VM。
- `Shutdown` 可重复调用。

## 7. RedisClientScriptVM

`RedisClientScriptVM` 继承 `ScriptVM`，只属于 `RedisClientThread`。

建议 custom pointer slots：

```cpp
enum RedisCustomPtrSlot {
  kRedisPtrThread = 1,
  kRedisPtrScriptVM = 2,
  kRedisPtrClient = 3,
  kRedisPtrContext = 4
};
```

VM 初始化时：

1. `ReserveCustomPtrSlots`。
2. `SetCustomPtr(kRedisPtrThread, this_thread)`。
3. `SetCustomPtr(kRedisPtrScriptVM, this)`。
4. `SetCustomPtr(kRedisPtrClient, &RedisClient::Instance())`。
5. 导出 Redis public API。
6. 加载 Redis 私有脚本。

注意：

- 外部不能获得 `RedisClientScriptVM*`。
- Redis 私有 VM 可以访问 Redis 模块全部 public API。
- 如果需要内部调试 API，应通过内部 binding 单独导出，不暴露到普通 VM。

## 8. Lua Binding 设计

所有拥有 Lua VM 的线程都可以导出 Redis API。

建议 Lua API：

```lua
redis.is_running()
redis.is_healthy()
redis.next_request_id()

redis.command({"PING"}, function(result)
  -- result.success
  -- result.status
  -- result.error
  -- result.value
end)

redis.command({"GET", "player:1"}, callback, {
  timeout_ms = 1000,
  trace_tag = "load_player"
})

redis.eval(script, keys, args, callback, {
  timeout_ms = 1000
})
```

binding 规则：

- Lua callback 存入当前 VM registry。
- callback ref 由当前 VM 所属 `AsyncResultDispatcher` 管理。
- Redis 线程回调只投递结果，不直接执行 Lua callback。
- 当前 VM 销毁时，dispatcher 必须释放所有 registry ref。
- 如果 callback 对应 VM 已销毁，结果丢弃并记录日志。

## 9. 公共异步结果派发基础设施

为满足草稿中“每个线程自己的结果队列”和“公共基础设施”的要求，建议新增 `AsyncResultDispatcher`。

职责：

- 为每个 VM 或线程保存一个结果队列。
- 支持任意线程向目标 dispatcher 投递任务。
- 只允许目标线程调用 `Dispatch`。
- 支持 C++ continuation 和 Lua callback 两类任务。
- VM 销毁或线程退出时可以 shutdown。

建议接口：

```cpp
class AsyncResultDispatcher {
 public:
  bool Enqueue(AsyncTask task);
  size_t Dispatch(size_t max_count);
  void Shutdown();
  bool IsShutdown() const;
};
```

需要接入的主循环：

- `Engine::FrameLoop`：主线程 VM。
- `Space::Update`：space VM。
- `DBThread::EventLoop`：DB 私有 VM。
- `PhysicsSystem::UpdateScript`：physics VM。
- `RedisClientThread`：Redis 私有 VM。

建议 dispatch 时机：

- 在对应 VM `UpdateScript` 前执行，这样脚本本帧可以看到异步结果。
- 每帧设置 `dispatch_batch_size`，避免单帧处理过多回调导致卡顿。

## 10. 配置设计

新增 `resources/config/server/redis.json`：

```json
{
  "connection": {
    "host": "127.0.0.1",
    "port": 6379,
    "password": "",
    "database": 0,
    "connect_timeout_ms": 5000,
    "command_timeout_ms": 5000,
    "keepalive": true
  },
  "queue": {
    "request_queue_size": 4096,
    "max_inflight": 4096,
    "dispatch_batch_size": 256
  },
  "script": {
    "redis_scripts_dir": "resources/script/redis",
    "auto_load": true
  },
  "log": {
    "enabled": true,
    "slow_command_ms": 100
  }
}
```

`resources/config/server/server.json` 增加：

```json
"redis": "resources/config/server/redis.json",
"redis_required": false
```

`ServerConfig` 增加：

```cpp
std::string redis;
bool redis_required = false;
```

配置校验：

- host 不能为空。
- port 必须在 1 到 65535。
- database 必须大于等于 0。
- connect timeout 和 command timeout 必须大于 0。
- request queue size 必须大于 0。
- max inflight 必须大于 0。
- dispatch batch size 必须大于 0。
- password 不建议在日志中输出。

## 11. CMake 集成方案

新增 server-only 开关：

```cmake
option(ENGINE_REDIS_ENABLED "Enable Redis runtime module" ON)
```

server target 中：

- 添加 hiredis subdirectory。
- 关闭 hiredis tests/examples。
- 链接 hiredis 和 libevent。
- 添加 `ENGINE_REDIS_ENABLED` compile definition。

runtime CMake 中：

- 只有 `ENGINE_REDIS_ENABLED` 且非 mobile 时追加 Redis 源码。
- client/mobile 必须显式不编译 Redis 源码。
- 避免因为 CMake cache 导致 client 误继承 server 的 Redis 开关。

建议增加内部判断变量，例如：

```cmake
set(ENGINE_REDIS_RUNTIME_ENABLED OFF)
if(ENGINE_REDIS_ENABLED AND NOT IS_MOBILE_PLATFORM)
  set(ENGINE_REDIS_RUNTIME_ENABLED ON)
endif()
```

如果 client target 会经过同一份 runtime CMake，client 侧需要显式将 Redis runtime disabled。

## 12. 引擎生命周期

### 12.1 初始化

建议顺序：

1. 加载 server config。
2. 初始化 logger。
3. 初始化主 EventLoop。
4. 初始化 Mongo / DatabaseService。
5. 加载 Redis config。
6. 初始化 `RedisClient`。
7. 创建主 `MainThreadScriptVM`。
8. 导出 runtime bindings，其中包含 Redis binding。

### 12.2 每帧更新

在 `Engine::FrameLoop` 中添加主 VM 的 async result dispatch：

```text
timer update
physics tick
config callbacks
async result dispatch
script_vm_->UpdateScript
space update
space message dispatch
physics result dispatch
```

具体位置可以放在 `script_vm_->UpdateScript` 前，保证 Redis 回调在本帧脚本更新前被处理。

### 12.3 关闭

Redis 应在 VM 销毁前停止：

```text
RedisClient::Shutdown
DatabaseService::Shutdown
MongoSystem::Shutdown
destroy VMs
destroy network/timer/logger
```

原因：

- Redis pending callback 可能持有 Lua registry ref。
- 如果先销毁 VM，再派发 Redis 回调，会造成悬挂引用。

## 13. 错误处理和重连策略

### 13.1 命令失败

Redis 命令错误，例如 `WRONGTYPE`，应作为 `kCommandError` 返回，callback 必须执行。

### 13.2 连接失败

连接失败时：

- `RedisClient::IsHealthy()` 返回 false。
- 如果 `redis_required = true`，server 初始化可以失败。
- 如果 `redis_required = false`，server 可以继续启动，但 Redis 请求返回 connection error。

### 13.3 超时

每个 pending request 记录 deadline。

Redis 线程通过定时 event 检查超时：

- 超时请求从 pending map 删除。
- callback 返回 `kTimeout`。
- 如果 hiredis 后续又返回该 request，必须识别为已完成并丢弃。

### 13.4 重连

第一版建议支持简单重连：

- disconnect callback 设置 healthy false。
- 定时重连。
- 重连成功后 healthy true。
- 断线期间新请求可以进入队列，但应受 max inflight 和 queue size 限制。

如果业务不需要排队等待重连，也可以配置为断线期间立即失败。

## 14. 不建议第一版支持的能力

以下能力会扩大复杂度，建议第二阶段再做：

- Redis Cluster。
- Sentinel。
- Pub/Sub。
- blocking commands，例如 `BLPOP`。
- pipeline 批量聚合。
- RESP3 完整类型支持。
- 多 Redis 连接池。

原因：

- Pub/Sub 和 blocking command 不适合与普通命令共用同一个 async context。
- Cluster/Sentinel 会显著增加连接管理复杂度。
- 当前草稿更关注线程模型、Lua VM 和公共派发机制。

## 15. 测试计划

### 15.1 单元测试

新增：

```text
src/tests/unit/database/test_redis_value.cpp
src/tests/unit/database/test_redis_dispatcher.cpp
src/tests/unit/database/test_redis_bind.cpp
```

覆盖：

- hiredis reply 到 `RedisValue` 的转换。
- array、nil、integer、error、status、bulk string。
- 二进制安全字符串。
- `AsyncResultDispatcher` 跨线程 enqueue，同线程 dispatch。
- dispatcher shutdown 后拒绝投递。
- Lua callback registry ref 生命周期。
- VM 销毁后结果不再回调 Lua。

### 15.2 集成测试

Redis 集成测试建议 opt-in：

- 环境变量 `ENGINE_REDIS_TEST_URL` 或 `ENGINE_REDIS_TEST_ENABLED=1`。
- 本地 Redis 可用时测试 `PING`、`SET`、`GET`、错误命令、超时、断线。
- 默认 CI 如果没有 Redis，不运行集成测试。

### 15.3 编译测试

必须覆盖：

- server target 编译包含 Redis。
- client target 不编译 Redis。
- mobile target 不编译 Redis。
- `ENGINE_REDIS_ENABLED=OFF` 时 server 可正常编译。

## 16. 实施步骤

### 阶段一：工程骨架

1. 增加 `ENGINE_REDIS_ENABLED`。
2. server CMake 接入 hiredis。
3. 新增 Redis 模块目录和空类。
4. 新增 Redis config 结构和配置文件。
5. 确认 client/mobile 不编译 Redis。

### 阶段二：公共派发基础设施

1. 实现 `AsyncResultDispatcher`。
2. 为主 VM、space VM、DB VM、physics VM 接入 dispatcher。
3. 增加 dispatcher 单元测试。

### 阶段三：Redis 核心线程

1. 实现 `RedisClient`。
2. 实现 `RedisRequest`、`RedisResult`、`RedisValue`。
3. 实现 `RedisClientThread`。
4. 接入 hiredis async 和 libevent adapter。
5. 实现 shutdown、timeout、connection error 处理。

### 阶段四：Lua VM 和 binding

1. 实现 `RedisClientScriptVM`。
2. 通过 `custom_ptr_store` 绑定 Redis 模块对象。
3. 实现 `bind/redis_bind.cc`。
4. 外部 VM 导出 `redis.command`、`redis.eval`、`redis.is_healthy`。
5. Lua callback 结果通过 dispatcher 回到所属线程。

### 阶段五：生命周期和可观测性

1. Engine 初始化 Redis。
2. Engine 关闭 Redis。
3. 增加 Redis health 和 stats。
4. 增加慢命令日志。
5. 增加连接失败和重连日志。

### 阶段六：测试和文档

1. 补单元测试。
2. 补 opt-in 集成测试。
3. 补 Lua 使用示例。
4. 更新 Redis 模块文档。

## 17. 风险和规避

### 17.1 回调线程错误

风险：hiredis callback 在 Redis 线程执行，如果直接调用外部 Lua VM，会造成线程安全问题。

规避：hiredis callback 只生成 `RedisResult`，然后投递到调用方 dispatcher。

### 17.2 VM 生命周期

风险：Lua callback ref 所属 VM 已销毁，但 Redis 结果稍后返回。

规避：dispatcher 使用 weak ownership；VM shutdown 时 dispatcher 标记关闭并释放 callback ref。

### 17.3 请求永不回调

风险：shutdown、超时、连接断开时 pending request 丢失。

规避：所有 accepted request 必须进入 pending 管理；关闭和超时统一生成失败结果。

### 17.4 client 误编译 Redis

风险：CMake cache 导致 client 继承 server 的 `ENGINE_REDIS_ENABLED`。

规避：使用 target-specific 变量或在 client/mobile 明确禁用 Redis runtime。

### 17.5 单线程吞吐瓶颈

风险：单个 `RedisClientThread` 无法满足高并发。

规避：第一版遵守草稿独立线程要求，先实现单线程；后续可扩展为多个 Redis worker 或多实例连接，但仍由 `RedisClient` 统一管理。

## 18. 推荐第一版交付边界

第一版交付以下能力：

- server-only 编译开关。
- RedisClient 公共单例。
- 独立 RedisClientThread。
- hiredis async + libevent。
- RedisClientScriptVM。
- Lua `redis.command`、`redis.eval`、`redis.is_healthy`。
- 跨线程 dispatcher。
- 请求成功、命令失败、连接失败、超时、关闭失败回调。
- 基础配置、日志、health、stats。
- 单元测试和可选 Redis 集成测试。

第一版不交付：

- Cluster。
- Sentinel。
- Pub/Sub。
- blocking command。
- 多连接池。
- pipeline 聚合优化。

