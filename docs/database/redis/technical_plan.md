# Redis 模块技术方案

> 来源：基于 `docs/database/redis/draft.txt` 的要求，并结合当前工程中 `data_service`、`physics`、`vm`、`config`、`thirdparty`、`CMake` 的现状整理。

## 1. 目标

在 server 端集成 hiredis，并在 `src/runtime/database/redis` 下实现一个线程安全的 Redis 访问模块。

核心目标：

- 只有 server 启用 Redis 模块，client 和 mobile 不启用。
- hiredis 和 libevent 全部运行在一个或多个独立线程 `RedisClientThread` 中，并由 `RedisClient` 统一管理。
- Redis 模块面向普通外部 C++/业务模块只暴露全局单例 `RedisClient`。
- 任意线程都可以通过 `RedisClient` 发起 Redis 请求。
- 请求通过 `moodycamel::ConcurrentQueue` 路由并传递给某个 `RedisClientThread`。
- RedisClient accepted 的请求，在成功、命令失败、连接错误、超时、关闭丢弃等情况下都要完成一次。
- Redis runtime 编译启用时，每个线程自己的 Lua VM 都可以导出访问 `RedisClient` 相关的 API。
- Redis 结果不能在 Redis 线程直接访问调用方 Lua VM，必须先进入调用方所属线程的结果队列，再由调用方线程主循环 dispatch。
- `RedisClientScriptVM` 是 Redis 线程私有 Lua VM，外部不可访问。
- `RedisClientScriptVM` 通过 `custom_ptr_store` 访问 Redis 模块级实例。
- 每个 `RedisClientThread` 的主循环默认 60 帧/秒，用于 timeout 检查、Redis 私有 dispatcher dispatch、`RedisClientScriptVM::UpdateScript()` 和脚本 frame callback；Redis IO 仍由 libevent/hiredis callback 驱动。
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

server 和 client 的 CMake option 有 cache 泄漏风险。如果同一构建树或上层构建入口先后配置 server/client，Redis 的 server-only 不能只依赖全局 option，需要在 client/mobile target 显式禁用或使用 target-specific 判断。

另外，现有 `MainThreadScriptVM` 对 Mongo/DB binding 的宏判断存在组合宏使用，Redis 模块应避免重复这种隐式判断，建议使用清晰的 `ENGINE_REDIS_ENABLED` 和 server-only target 条件。

## 3. 总体架构

```text
调用线程 / 调用方 Lua VM
  -> 访问 RedisClient 的 Lua binding 或 C++ RedisClient API
  -> RedisClient 全局单例，线程安全
       - 统一管理 RedisClientThread worker 集合
       - 生成全局 request_id
       - 按 routing_key 或 round-robin 选择 worker
  -> worker 自己的 moodycamel::ConcurrentQueue<RedisRequest>
  -> RedisClientThread[n] 独立线程
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

- `RedisClientThread` 可以有多个；每个 worker 都是独立 Redis IO 线程，拥有自己的 libevent loop、hiredis async context、请求队列、pending map 和 Redis 私有 VM。
- `RedisClient` 是普通外部 C++/业务模块的唯一入口，也是 `RedisClientThread` 集合的统一管理者，负责启动、关闭、reload、路由、health 和 stats 聚合。
- `redisAsyncContext` 只在所属 `RedisClientThread` 中访问。
- hiredis 回调只在所属 `RedisClientThread` 中执行。
- 每个 `RedisClientThread` 的主循环默认 60 帧/秒；该频率只影响周期性 tick，不影响 wakeup event 和 hiredis IO callback 的即时处理。
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
  redis_client_thread_group.h
  redis_client_thread_group.cc
  redis_client_script_vm.h
  redis_client_script_vm.cc
  redis_request.h
  redis_result.h
  redis_value.h
  redis_connection.h
  redis_connection.cc
  module_access.h
  bind/
    redis_bind.h
    redis_bind.cc

src/runtime/config/
  redis_config.h
  redis_config.cc

src/runtime/vm/
  async_result_dispatcher.h
  async_result_dispatcher.cc

resources/config/server/
  redis.json

resources/script/redis/
  init.lua
```

`AsyncResultDispatcher` 建议放在 `src/runtime/vm`，并加入 `VM_SOURCES`。原因是它需要管理 Lua registry ref 和 VM owner thread 语义；放在 `core` 会让 core 层直接依赖 Lua 回调生命周期，不符合当前模块边界。如果后续拆出纯 C++ 异步队列，再单独放入 `core/async`。

`RedisClientConfig` 建议放在 `src/runtime/config/redis_config.h`，而不是放在 `database/redis` 内。原因是 `ConfigManager` 需要在公共配置层解析、缓存和 diff Redis 配置；把配置结构放在 config 模块可以避免 `config.h` 反向依赖 server-only 的 Redis runtime 实现。`database/redis` 只消费该配置结构，不把 hiredis/libevent 细节泄漏到配置层。

## 5. 公共 API 设计

### 5.1 RedisClient

`RedisClient` 是 Redis 模块面向普通外部 C++/业务模块的唯一可见入口。

`RedisSubmitResult`、`RedisCommandOptions`、`RedisResult`、`RedisValue` 等只作为 `RedisClient` API 的数据传输类型公开；除 `RedisClient` 外，不再暴露其他可调用服务类或模块级实例。
Redis 私有 VM 的内部 binding 可以访问 Redis 模块 public API，但这不改变普通外部模块只能通过 `RedisClient` 访问 Redis 的边界。

建议接口：

```cpp
struct RedisClientConfig;
struct RedisResult;

using RedisCompletion = std::function<void(RedisResult&&)>;

struct RedisCommandOptions {
  int timeout_ms = 0;       // 0 = use config.connection.command_timeout_ms
  std::string trace_tag;    // optional, for logs/metrics only
  std::string routing_key;  // optional, stable within current worker generation
};

enum class RedisSubmitStatus {
  kAccepted,
  kInvalidArgument,
  kUnsupportedCommand,
  kNotRunning,
  kDisconnected,
  kQueueFull,
  kShuttingDown
};

struct RedisSubmitResult {
  bool accepted = false;
  uint64_t request_id = 0;
  RedisSubmitStatus status = RedisSubmitStatus::kNotRunning;
  std::string error;
};

enum class RedisClientState {
  kDisabled,
  kStarting,
  kConnecting,
  kHealthy,
  kDegraded,
  kReconnecting,
  kStopping,
  kStopped,
  kError
};

struct RedisClientStats {
  struct Worker {
    size_t worker_index = 0;
    bool running = false;
    bool healthy = false;
    size_t queued_requests = 0;
    size_t inflight_requests = 0;
    uint64_t completed_requests = 0;
    uint64_t timed_out_requests = 0;
  };

  RedisClientState state = RedisClientState::kDisabled;
  size_t worker_count = 0;
  size_t healthy_worker_count = 0;
  size_t unhealthy_worker_count = 0;
  size_t queued_requests = 0;
  size_t inflight_requests = 0;
  uint64_t accepted_requests = 0;
  uint64_t rejected_requests = 0;
  uint64_t completed_requests = 0;
  uint64_t timed_out_requests = 0;
  std::vector<Worker> workers;
};

struct RedisClientStartOptions {
  bool wait_for_initial_connect = false;
};

class RedisClient {
 public:
  static RedisClient& Instance();

  bool Initialize(
      const RedisClientConfig& config,
      RedisClientStartOptions options = {});
  void Shutdown();

  bool IsRunning() const;
  bool IsHealthy() const;
  RedisClientStats GetStats() const;

  RedisSubmitResult Command(
      std::vector<std::string> argv,
      RedisCompletion completion,
      RedisCommandOptions options = {});

  RedisSubmitResult Eval(
      std::string script,
      std::vector<std::string> keys,
      std::vector<std::string> args,
      RedisCompletion completion,
      RedisCommandOptions options = {});
};
```

要求：

- `Initialize` 在当前运行 generation 内只能成功执行一次；运行中或正在停止时再次调用必须失败并记录原因。`Initialize` 失败完整回滚后，或 `Shutdown` 完成并进入 `kStopped` / `kDisabled` 后，允许再次调用，用于修正配置后重试或 reload 从 disabled 重新启用 Redis。
- 如果 `Initialize` 失败，必须完整回滚线程、event_base、hiredis context、dispatcher 和统计状态，使调用方可以修正配置后重试初始化。
- `Initialize` 不应从 `ConfigManager` 隐式读取 `redis_required`；Engine 读取 `ServerConfig.redis_required` 后，通过 `RedisClientStartOptions::wait_for_initial_connect` 显式传入启动策略。
- `Shutdown` 可重复调用，必须幂等。
- `Command` 和 `Eval` 可由任意线程调用。
- `RedisClient` 内部统一管理一个或多个 `RedisClientThread`；外部调用方不能直接指定 worker id，也不能访问 worker 实例。
- `Command` 默认按 round-robin 路由到 worker；如果 `RedisCommandOptions::routing_key` 非空，则使用稳定 hash 路由到同一个 worker，便于同一业务键保持同连接内顺序。
- 普通外部 `Eval` / `redis.eval` 如果未显式设置 `routing_key` 且 `keys` 非空，应默认使用第一个 key 作为 routing key；无 key 的 eval 才走 round-robin。Redis 私有 VM 或内部维护任务设置了 `preferred_worker_index` 时，该内部 preferred worker 优先于 Eval 默认 key。
- `routing_key` 的稳定性只保证在当前 worker 集合 generation 和当前 `thread_count` 内成立；`thread_count` reload 后，同一个 key 可能映射到不同 worker。
- 多个 worker 之间不保证全局命令顺序。需要严格顺序的调用方应使用同一个 `routing_key`，或在上一个请求 completion 后再提交下一个请求。
- 未指定 `routing_key` 时，round-robin 应优先只选择当前 healthy 的 worker；没有 healthy worker 且 `queue_while_disconnected = false` 时同步拒绝为 `kDisconnected`。没有 healthy worker 但 `queue_while_disconnected = true` 时，可以在仍 running 的 worker 中 round-robin 排队；如果 worker 集合不存在或没有 running worker，则同步拒绝为 `kNotRunning` / `kDisconnected`。
- 指定 `routing_key` 时不能因为目标 worker unhealthy 而自动改投其他 worker，否则会破坏同 key 顺序；目标 worker unhealthy 且 `queue_while_disconnected = false` 时同步拒绝为 `kDisconnected`。
- `queue_while_disconnected = true` 时，目标 worker unhealthy 的请求可以进入该 worker 的队列，但仍受全局 queue、全局 inflight、单请求 timeout 和 shutdown 状态限制。
- 返回 `RedisSubmitResult.accepted = true` 表示请求已被 RedisClient accepted，必须保证最终 completion 一次，并返回本次请求的 `request_id`。
- 返回 `accepted = false` 表示请求未被接受，不触发 completion；`status` 和 `error` 描述同步拒绝原因。Lua binding 应把这种情况转换为同步错误返回。
- 队列满、模块未启动、正在关闭、未连接且禁止断线排队、参数非法或 completion 为空时返回 `accepted = false`。
- `Command` 必须在入队前解析并规范化第一个 argv 作为命令名；空命令、空命令名或命令名包含二进制控制字符时返回 `kInvalidArgument`。
- 第一版必须拒绝会改变连接状态、阻塞 worker、或把连接切入专用模式的命令，并返回 `kUnsupportedCommand`，不触发 completion。拒绝列表至少包含 `AUTH`、`HELLO`、`SELECT`、`QUIT`、`RESET`、`CLIENT`、`MONITOR`、`SUBSCRIBE`、`PSUBSCRIBE`、`SSUBSCRIBE`、`UNSUBSCRIBE`、`PUNSUBSCRIBE`、`SUNSUBSCRIBE`、`BLPOP`、`BRPOP`、`BRPOPLPUSH`、`BLMOVE`、`BZPOPMIN`、`BZPOPMAX`、`BZMPOP`、带 `BLOCK` 选项的 `XREAD` / `XREADGROUP`、`MULTI`、`EXEC`、`DISCARD`、`WATCH`、`UNWATCH`。
- 命令校验逻辑应集中在 RedisClient 内部 helper 中，Lua binding 和 C++ API 都调用同一条路径；命令名比较使用 ASCII case-insensitive 规则，`XREAD` / `XREADGROUP` 只在 `STREAMS` 之前的 option 区间扫描 `BLOCK` 选项，避免把 key 名误判为 blocking 选项。
- 连接认证和 database 选择只能由 `RedisClientThread` 根据配置执行，普通 `Command` 和 Lua binding 不能覆盖。第一版固定使用 hiredis 默认 RESP2，不开放 `HELLO`；如果后续支持 RESP 协议版本切换，也只能通过 Redis 配置完成。
- `RedisCompletion` 是 RedisClient 的低层完成回调，默认在 `RedisClientThread` 中执行，必须线程安全，且不能直接访问任何外部 Lua VM。
- Lua binding 和非线程安全 C++ 调用方必须使用 `AsyncResultDispatcher` 包装 completion，把最终业务回调投递回调用方所属线程。

### 5.2 RedisRequest

`RedisRequest` 是 Redis 模块内部传输结构，用于 `RedisClient` 向 worker 队列投递请求；它不属于外部可调用 API，也不应被普通业务模块直接构造或保存。

建议字段：

```cpp
struct RedisRequest {
  uint64_t request_id = 0;
  std::vector<std::string> argv;
  RedisCommandOptions options;
  RedisCompletion completion;
  std::optional<size_t> preferred_worker_index;  // internal only
};
```

`argv` 使用 `std::string` 保存参数，hiredis 调用时使用 `redisAsyncCommandArgv`，保证二进制安全。

不建议在 `RedisRequest` 中直接保存 Lua registry ref。Lua ref 应由调用方线程的 dispatcher/registry 管理，Redis 请求只保存一个线程安全 completion adapter。

`preferred_worker_index` 只允许 Redis 模块内部通过 internal submit helper 或 request builder 设置，用于 Redis 私有 VM 或内部维护任务在没有显式 `routing_key` 时把请求固定回所属 worker；普通外部 API 不暴露 worker id。

### 5.3 RedisResult

建议字段：

```cpp
enum class RedisResultStatus {
  kOk,
  kCommandError,
  kConnectionError,
  kAuthError,
  kProtocolError,
  kTimeout,
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

`RedisClientThread` 是 Redis 模块内部线程类，外部不可直接访问。`RedisClient` 可以创建并统一管理多个 `RedisClientThread` worker；每个 worker 独立连接同一份 Redis 配置，拥有自己的请求队列、pending map、event_base、hiredis context 和 `RedisClientScriptVM`。

内部成员建议：

```cpp
class RedisClientThread {
 public:
  bool Start(size_t thread_index, const RedisClientConfig& config);
  void Stop();
  bool Enqueue(RedisRequest request);
  bool IsHealthy() const;

 private:
  void ThreadMain();
  void Connect();
  void Disconnect();
  void Tick();
  void DrainRequests();
  void CompleteRequest(uint64_t request_id, RedisResult result);
};
```

线程内持有：

- `std::thread thread_`
- `size_t thread_index_`
- `event_base* event_base_`
- `redisAsyncContext* redis_context_`
- `RedisClientScriptVM script_vm_`
- `moodycamel::ConcurrentQueue<RedisRequest> request_queue_`
- `std::unordered_map<uint64_t, PendingRedisRequest> pending_requests_`
- main loop tick event，默认 60 fps，使用微秒级或更高精度计算 interval
- wakeup event
- logger
- stats
- atomic running/stopping/healthy state

### 6.1 启动流程

1. `RedisClient::Initialize` 校验 `thread.thread_count`，创建 `RedisClientThread` worker 集合。
2. `RedisClientThread::Start` 为每个 worker 创建独立线程。
3. 线程名设置为 `RedisClientThread`，保持与草稿要求一致；多 worker 的 index 写入日志、stats 和 profiler context，不依赖 OS thread name 区分。
4. 线程内创建 `event_base`。
5. 初始化 `RedisClientScriptVM`。
6. 加载 Redis 线程脚本目录。
7. 创建 `redisAsyncContext`。
8. 调用 `redisLibeventAttach(redis_context_, event_base_)`。
9. 设置 connect callback 和 disconnect callback。
10. 注册 wakeup event。
11. 注册周期性 tick event，间隔由 `thread.main_loop_fps` 计算，默认 60 fps；使用微秒级或更高精度计算，避免 `1000 / 60` 整数毫秒截断成 16ms。
12. 进入 `event_base_dispatch`。

连接握手：

- connect callback 成功只表示 TCP/RESP 连接建立，不代表 RedisClient healthy。
- 如果配置了 password 或 username，必须先发送 `AUTH`。username 非空时使用 `AUTH username password`；username 为空但 password 非空时使用 `AUTH password`。
- 如果 `database > 0`，AUTH 成功后发送 `SELECT database`。
- 只有 AUTH 和 SELECT 都成功后，`IsHealthy()` 才能返回 true，Redis 线程才可以 drain 外部命令请求。
- AUTH 或 SELECT 失败应完成握手为 `kAuthError` 或 `kConnectionError`，设置 unhealthy，并按重连策略处理。

启动同步策略：

- `Start` 应等待每个 Redis 线程完成基础初始化，也就是 `event_base`、wakeup fd、tick event、`RedisClientScriptVM` 创建完成。
- 如果任一 worker 基础初始化失败，`RedisClient::Initialize` 必须停止并清理已经启动的 worker，然后返回 false。
- 如果 `wait_for_initial_connect = true`，`RedisClient::Initialize` 还应等待所有 worker 首次连接完成，等待时间由 `connect_timeout_ms` 控制；任一 worker 连接失败则返回 false，并清理整个 worker 集合。
- 多 worker 的首次连接应并发发起；`connect_timeout_ms` 是本次 Initialize 的整体等待预算，不应按 worker 数串行累加。
- 如果 `wait_for_initial_connect = false`，`Initialize` 可以在所有 worker 基础设施初始化成功后返回 true，即使首次连接失败，也通过后台重连和 health 状态反映。
- `RedisClient::IsHealthy()` 默认要求所有 worker 都 healthy；如果只有部分 worker healthy，`RedisClientState` 应为 `kDegraded`，stats 中记录 unhealthy worker 数量。

### 6.2 请求流程

1. 调用方线程调用 `RedisClient::Command`。
2. `RedisClient` 校验参数和 completion，并生成 `request_id`。
3. `RedisClient` 根据显式 `routing_key`、内部 `preferred_worker_index`、`Eval` 默认 key 或 round-robin 选择目标 worker；优先级为显式 `routing_key` > `preferred_worker_index` > `Eval` 默认 key > round-robin。
4. 请求入目标 worker 的 `request_queue_`。
5. 唤醒目标 Redis 线程。
6. Redis 线程 drain 队列。
7. 对每个请求调用 `redisAsyncCommandArgv`。
8. 请求进入该 worker 的 `pending_requests_`。
9. hiredis 回调触发。
10. hiredis reply 转换成 `RedisResult`。
11. 从 `pending_requests_` 删除请求。
12. 调用 request completion。
13. 如果该 completion 是 Lua binding 或 dispatcher 包装器，则只把结果投递给调用方 dispatcher；真正的 Lua/C++ 业务回调在调用方线程 dispatch 时执行。

背压要求：

- `moodycamel::ConcurrentQueue` 本身不应被当作容量控制来源；`request_queue_size` 和 `max_inflight` 必须由 RedisClient 维护全局原子计数，同时由各 worker 维护本地计数用于 stats 和调试。
- `request_queue_size` 限制 RedisClient 已 accepted 但尚未被任一 Redis 线程 drain 的请求总数。
- `max_inflight` 限制所有 worker 中已经发送给 hiredis、正在等待 Redis reply 的请求总数。
- `request_queue_size` 和 `max_inflight` 是两个独立上限，不要求彼此大小关系；内存预算应按二者之和估算。
- 多 worker 场景下全局上限是 hard cap。每个 worker 还应记录本地 queued/inflight 计数并写入 stats，便于发现 `routing_key` 热点导致的单 worker 倾斜；第一版不额外增加 per-worker 容量配置。
- accepted 后如果 Redis 线程发送失败，仍必须完成为 `kConnectionError` 或 `kProtocolError`，不能把失败退回成同步拒绝。

跨线程唤醒要求：

- 调用方线程不能直接操作 `event_base` 或 libevent `event*`，除非显式启用了 libevent thread support。
- 第一版建议使用 `evutil_socketpair`、pipe 或 eventfd 作为 wakeup fd；调用方只写入 wakeup fd，Redis 线程中的 libevent read event 负责 drain 请求队列。
- Windows 下优先使用 libevent 提供的 `evutil_socketpair`；如果不可用，再封装 loopback socket。

发送门槛：

- 外部请求只有在该 worker 已完成连接握手、AUTH/SELECT 成功且 `healthy = true` 后，才能发送给 hiredis。
- `queue_while_disconnected = true` 时，unhealthy worker 可以保留已 accepted 但尚未发送的请求；这些请求在排队期间仍按 accepted 时间计算 timeout，超时后完成为 `kTimeout`，不能等到重连后再发送。
- worker 重新 healthy 后才能继续 drain 这些排队请求并发送给 hiredis；如果 shutdown 先发生，则排队请求完成为 `kShutdown`。

### 6.3 关闭流程

1. 设置 stopping。
2. RedisClient 拒绝新请求。
3. 唤醒所有 Redis 线程。
4. 每个 worker 对 request queue 中尚未发送的请求生成 shutdown 失败结果。
5. 每个 worker 对 pending requests 生成 shutdown 或 connection error 失败结果。
6. 每个 worker 调用 `redisAsyncDisconnect`。
7. 每个 worker 调用 `event_base_loopbreak`。
8. join 所有线程。
9. 释放所有 `redisAsyncContext`、tick event、wakeup event 和 `event_base`。

关闭必须保证：

- 不遗留 pending completion。
- 不访问已销毁 VM。
- `Shutdown` 可重复调用。

## 7. RedisClientScriptVM

`RedisClientScriptVM` 继承 `ScriptVM`，只属于一个 `RedisClientThread` 实例。多个 `RedisClientThread` worker 会各自拥有独立的 Redis 私有 VM，彼此不共享 Lua state。

建议 custom pointer slots：

```cpp
enum RedisCustomPtrSlot {
  kRedisPtrThread = 1,
  kRedisPtrScriptVM = 2,
  kRedisPtrClient = 3,
  kRedisPtrDispatcher = 4
};
```

VM 初始化时：

1. `ReserveCustomPtrSlots`。
2. `SetCustomPtr(kRedisPtrThread, this_thread)`。
3. `SetCustomPtr(kRedisPtrScriptVM, this)`。
4. `SetCustomPtr(kRedisPtrClient, &RedisClient::Instance())`。
5. 创建属于 Redis 线程的 `AsyncResultDispatcher`，并通过 `kRedisPtrDispatcher` 注册。
6. 导出 Redis 私有 VM 专用 binding：包含访问 `RedisClient` 的 API，以及 `src/runtime/database/redis` 中允许脚本访问的 public API 包装。
7. 加载 Redis 私有脚本。

注意：

- 外部不能获得 `RedisClientScriptVM*`。
- Redis 私有 VM 可以访问 Redis 模块全部 public API；这些 API 只通过 Redis 私有 VM 专用 binding 暴露，不进入普通 VM 的 `redis` 表，也不暴露 hiredis context 或 `RedisClientThread` 实例。
- 不要把 raw `redisAsyncContext*` 暴露到 Lua custom ptr store；hiredis context 只允许 `RedisClientThread` 内部 C++ 代码访问。
- 如果需要内部调试 API，应通过内部 binding 单独导出，不暴露到普通 VM。
- Redis 私有 VM 发起的 Redis 请求，其 completion 也必须进入 Redis 线程自己的 dispatcher，再由 Redis 线程主循环 dispatch，不能在 hiredis callback 栈上直接调用 Lua。
- Redis 私有 VM 未显式指定 `routing_key` 时，请求应通过内部 `preferred_worker_index` 固定回所属 `RedisClientThread`；即使 `redis.eval` 带有非空 keys，也不应先合成默认 key 路由覆盖 `preferred_worker_index`。这样可以避免私有脚本请求被默认 round-robin 或 Eval 默认 key 投递到其他 worker。
- `RedisClientThread` 需要注册一个周期性 tick event，用于执行 timeout 检查、Redis 私有 dispatcher dispatch、`RedisClientScriptVM::UpdateScript()` 和脚本 frame callback；tick 频率由 `thread.main_loop_fps` 控制，默认 60 fps。

## 8. Lua Binding 设计

Redis runtime 编译启用时，所有拥有 Lua VM 的线程都可以导出访问 `RedisClient` 相关的 Lua API。这里的“普通 VM 导出”只表示导出 `RedisClient` 对外入口的 Lua 包装，不表示普通 VM 可以访问 Redis 模块内部类、`RedisClientThread`、`RedisClientScriptVM` 或 hiredis context。

建议 Lua API：

```lua
redis.is_running()
redis.is_healthy()

local ok, request_id_or_error = redis.command({"PING"}, function(result)
  -- result.success
  -- result.status
  -- result.error
  -- result.value
end)

redis.command({"GET", "player:1"}, callback, {
  timeout_ms = 1000,
  trace_tag = "load_player",
  routing_key = "player:1"
})

redis.eval(script, keys, args, callback, {
  timeout_ms = 1000
})
```

binding 规则：

- 只有编译启用了 Redis runtime 时，普通 VM 才导出 `redis` 表。Redis runtime 未编译时，不导出 `redis.command` / `redis.eval` / `redis.is_running` / `redis.is_healthy`。
- Redis runtime 已编译但 server 配置未启用 Redis 或 `RedisClient` 未初始化时，`redis.is_running()` 返回 false，`redis.command` / `redis.eval` 同步返回 `false, error`，不保存 callback。
- `redis.is_running()` 只表示 `RedisClient` 已初始化且 worker 集合存在；`redis.is_healthy()` 表示所有 worker 都 healthy。Redis disabled 时二者都返回 false；部分 worker unhealthy 时 `is_running()` 返回 true、`is_healthy()` 返回 false。
- `redis.command` / `redis.eval` 同步返回 `ok, request_id_or_error`。请求未被 RedisClient accepted 时，`ok = false`，不会产生异步回调；如果 binding 为了构造 completion 已经临时注册 Lua callback，必须在 owner thread 立即 `luaL_unref`。
- Lua options 中的 `routing_key` 透传到 `RedisCommandOptions::routing_key`；`redis.command` 未指定时由 `RedisClient` round-robin 路由，不保证跨请求顺序。
- 普通 VM 的 `redis.eval` 未显式指定 `routing_key` 且 `keys` 非空时，binding 应使用 `keys[1]` 作为默认 routing key；Redis 私有 VM 的 binding 如果带有内部 `preferred_worker_index`，不得用该默认 key 覆盖 preferred worker。
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
- Lua registry ref 只能在 dispatcher owner thread 创建和释放，不能由 Redis 线程析构释放。

建议接口：

```cpp
using AsyncCallbackId = uint64_t;
using AsyncTask = std::function<void()>;
using LuaArgPusher = std::function<int(lua_State*)>;

class AsyncResultDispatcher {
 public:
  AsyncCallbackId RegisterLuaCallback(lua_State* L, int function_index);
  bool Enqueue(AsyncTask task);
  bool EnqueueLuaCallback(AsyncCallbackId callback_id, LuaArgPusher push_args);
  size_t Dispatch(size_t max_count);
  void ShutdownOnOwnerThread();
  bool IsShutdown() const;
};
```

Lua binding 使用方式：

1. 在调用方线程通过 `RegisterLuaCallback` 保存 Lua function，得到 `AsyncCallbackId`。
2. Redis 请求的 `RedisCompletion` 捕获 `weak_ptr<AsyncResultDispatcher>` 和 `AsyncCallbackId`。
3. Redis 线程收到结果后，只调用 `dispatcher->EnqueueLuaCallback(callback_id, push_args)`；`push_args` 是 Redis binding 创建的闭包，负责在 owner thread 把 `RedisResult` 转成 Lua table。
4. 调用方线程主循环执行 `Dispatch`，取出结果并调用 Lua function。
5. callback 调用完成后，由 dispatcher 在 owner thread 调用 `luaL_unref`。
6. dispatcher shutdown 时，在 owner thread 统一释放仍未完成的 Lua refs。

禁止事项：

- 禁止跨线程保存 `lua_State*` 后在 Redis 线程调用 Lua API。
- 禁止把会在 Redis 线程析构的对象设计成自动 `luaL_unref`。
- 禁止在 dispatcher 已 shutdown 后继续接受 Lua result；这种结果只记录 debug 日志并丢弃。
- 禁止让公共 `AsyncResultDispatcher` 依赖 `RedisResult`、`RedisValue` 或任何 Redis 模块头；Redis 结果到 Lua table 的转换必须留在 `database/redis/bind` 内。

需要接入的主循环：

- `Engine::FrameLoop`：主线程 VM。
- `Space::Update`：space VM。
- `DBThread::EventLoop`：DB 私有 VM。
- `PhysicsSystem::UpdateScript`：physics VM。
- 每个 `RedisClientThread`：各自的 Redis 私有 VM。

建议 dispatch 时机：

- 在对应 VM `UpdateScript` 前执行，这样脚本本帧可以看到异步结果。
- 当前主线程 `Engine::FrameLoop` 中应放在 `script::FlushConfigCallbacks` 之后、`script_vm_->UpdateScript()` 之前；`Space::Update` 中应放在 `vm_->UpdateScript()` 之前。
- 每帧设置 `dispatch_batch_size`，避免单帧处理过多回调导致卡顿。
- shutdown 阶段由 owner thread 调用 `ShutdownOnOwnerThread`，释放 callback ref；不要依赖跨线程析构释放 Lua 资源。

## 10. 配置模块设计

Redis 配置模块由三部分组成：

- `server.json` 中的 Redis 配置入口和启动策略。
- `resources/config/server/redis.json` 中的 Redis 详细配置。
- C++ 侧 `RedisClientConfig`、`ConfigManager` 加载接口、校验逻辑和 Lua config binding。

### 10.1 配置文件入口

`resources/config/server/server.json` 增加：

```json
"redis": "",
"redis_required": false
```

含义：

- `redis`：Redis 详细配置文件路径，路径规则与当前 `db_service`、`mongodb_dev`、`mongodb_public` 保持一致，使用相对工作目录路径。
- `redis_required`：Redis 是否是 server 启动和 readiness 的强依赖。

启动策略：

- Redis runtime 未编译时，如果 `redis_required = true`，配置校验失败，server 初始化失败；如果 `redis_required = false`，允许保留 `server.redis` 字段但不加载 Redis 详细配置文件，也不创建 `RedisClientThread` worker 集合。
- `redis` 为空且 `redis_required = false` 时，不启动 Redis 模块。
- `redis` 为空且 `redis_required = true` 时，配置校验失败，server 初始化失败。
- `redis` 非空但加载失败：
  - `redis_required = true`：server 初始化失败。
  - `redis_required = false`：server 继续启动，Redis 状态为 error，且不创建 `RedisClientThread` worker 集合；`RedisClient` 不进入 running 状态，提交请求同步返回 `kNotRunning`，Lua binding 同步返回 `false, error`。
- `redis` 加载成功但连接失败：
  - `redis_required = true`：server 初始化失败或 readiness fail。
  - `redis_required = false`：server 继续启动，Redis 请求返回 connection error，并按重连策略后台重连。

`ServerConfig` 增加：

```cpp
// Redis client config file path (relative to working dir).
std::string redis;
bool redis_required = false;
```

同时需要更新：

- `src/runtime/config/config.h`
- `src/runtime/config/config.cc`
- `src/runtime/config/config_validator.cc`
- `src/runtime/config/bind/config_bind.cc`
- `resources/config/server/server.json`

### 10.2 Redis 详细配置文件

新增 `resources/config/server/redis.json`：

```json
{
  "connection": {
    "host": "127.0.0.1",
    "port": 6379,
    "username": "",
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
  "thread": {
    "thread_count": 1,
    "main_loop_fps": 60
  },
  "script": {
    "redis_scripts_dir": "resources/script/redis",
    "auto_load": true
  },
  "log": {
    "enabled": true,
    "slow_command_ms": 100
  },
  "reconnect": {
    "enabled": true,
    "initial_delay_ms": 500,
    "max_delay_ms": 5000,
    "backoff_multiplier": 2,
    "queue_while_disconnected": false
  }
}
```

默认保持空字符串，避免旧环境升级后因为缺少 Redis 服务而出现启动噪声或后台重连。需要启用 Redis 时，再在 server 配置中显式写入：

```json
"redis": "resources/config/server/redis.json"
```

### 10.3 C++ 配置结构

新增 `src/runtime/config/redis_config.h`：

```cpp
struct RedisConnectionConfig {
  std::string host = "127.0.0.1";
  int port = 6379;
  std::string username;
  std::string password;
  int database = 0;
  int connect_timeout_ms = 5000;
  int command_timeout_ms = 5000;
  bool keepalive = true;
};

struct RedisQueueConfig {
  size_t request_queue_size = 4096;
  size_t max_inflight = 4096;
  size_t dispatch_batch_size = 256;
};

struct RedisThreadConfig {
  size_t thread_count = 1;
  int main_loop_fps = 60;
};

struct RedisScriptConfig {
  std::string redis_scripts_dir = "resources/script/redis";
  bool auto_load = true;
};

struct RedisLogConfig {
  bool enabled = true;
  int slow_command_ms = 100;
};

struct RedisReconnectConfig {
  bool enabled = true;
  int initial_delay_ms = 500;
  int max_delay_ms = 5000;
  int backoff_multiplier = 2;
  bool queue_while_disconnected = false;
};

struct RedisClientConfig {
  RedisConnectionConfig connection;
  RedisQueueConfig queue;
  RedisThreadConfig thread;
  RedisScriptConfig script;
  RedisLogConfig log;
  RedisReconnectConfig reconnect;
};
```

如果后续需要支持多 Redis 实例，可以扩展为：

```cpp
std::vector<RedisClientConfig> clients;
```

但第一版建议只支持单 Redis 实例配置；`thread.thread_count > 1` 只是为同一实例创建多个受控 worker，不引入多实例命名空间、多实例 Lua API 或通用连接池语义。

### 10.4 ConfigManager 接口

`ConfigManager` 增加静态加载接口：

```cpp
static bool LoadRedisClientConfigFromFile(
    const std::string& path,
    RedisClientConfig& out);
```

实例侧增加缓存和访问接口：

```cpp
bool LoadRedisClientConfigLocked(RedisClientConfig& out) const;
bool IsRedisConfigLoaded() const;
RedisClientConfig GetRedisClientConfig() const;
```

`ConfigManager` 内部新增成员：

```cpp
RedisClientConfig redis_config_;
bool redis_loaded_ = false;
```

加载流程建议：

1. `LoadServerFromFile` 先解析 `server.json`；`LoadServerFromString` 也应复用同一套 Redis 入口校验和详细配置加载逻辑。
2. 通过编译宏或 server target 注入的 helper 判断 Redis runtime 是否已编译进当前 target。
3. Redis runtime 未编译且 `server.redis_required = true` 时，server 配置加载必须失败；Redis runtime 未编译且 `server.redis_required = false` 时，跳过 Redis 详细配置加载，即使 `server.redis` 非空也不打开该文件，并设置 `redis_loaded_ = false`。
4. Redis runtime 已编译且 `server.redis` 非空时，调用 `LoadRedisClientConfigFromFile(server.redis, new_redis)`。
5. `LoadRedisClientConfigFromFile` 内部执行 `InterpolateConfigStrings(out)`，允许 password 使用环境变量或配置插值。
6. Redis 配置加载成功后，写入 `redis_config_` 和 `redis_loaded_`。
7. Redis 配置加载失败时，根据 `server.redis_required` 决定 server 配置加载是否失败；如果 `redis_required = false`，server 配置加载可以成功，但必须记录错误并设置 `redis_loaded_ = false`。
8. 日志中禁止输出明文 password；如 password 看起来是明文，应复用 Mongo 配置的 plaintext credential warning 思路。

建议在 `src/runtime/config/config_constants.h` 增加默认路径：

```cpp
inline constexpr const char* kRedisConfigFile = "/server/redis.json";
```

如果保持当前配置文件中的完整相对路径，也可以不依赖该常量；但新增常量有利于默认值、测试和文档统一。

实现注意：

- `config.h` 如果需要按值缓存 `RedisClientConfig`，应包含 `runtime/config/redis_config.h`。
- `redis_config.h` 不能包含 hiredis、libevent 或 `database/redis` 内部头文件。
- 如果后续决定不在 `ConfigManager` 缓存 Redis 详细配置，则 `redis.json` 变化不会自然进入 `ConfigChangeSet`；这种实现必须让 `RedisClient` 自己订阅文件变化并重新加载，复杂度更高，不建议第一版采用。

### 10.5 热重载策略

当前 `ConfigManager` 已支持 reload callback。Redis 配置应接入同一套机制。

server reload 时：

- 比较 `server.redis` 路径变化。
- 比较 `server.redis_required` 变化。
- Redis runtime 已编译且 Redis 配置文件内容变化时，生成 `ConfigChange`。

建议新增 change keys：

```text
server.redis
server.redis_required
redis.connection.host
redis.connection.port
redis.connection.username
redis.connection.password
redis.connection.database
redis.connection.connect_timeout_ms
redis.connection.command_timeout_ms
redis.queue.request_queue_size
redis.queue.max_inflight
redis.queue.dispatch_batch_size
redis.thread.thread_count
redis.thread.main_loop_fps
redis.script.redis_scripts_dir
redis.script.auto_load
redis.log.enabled
redis.log.slow_command_ms
redis.reconnect.enabled
redis.reconnect.initial_delay_ms
redis.reconnect.max_delay_ms
redis.reconnect.backoff_multiplier
redis.reconnect.queue_while_disconnected
```

RedisClient reload 行为：

- Redis runtime 未编译：`server.redis` 变化只更新配置字段，不加载 Redis 详细配置、不启动 `RedisClient`；如果新的 `redis_required = true`，reload 校验失败并保留旧配置。
- Redis runtime 已编译且 `server.redis` 从空变为非空：加载 Redis config 后启动 `RedisClient`；若加载或启动失败，按新的 `redis_required` 规则处理 readiness 和错误日志。
- `server.redis` 从非空变为空且 `redis_required = false`：调用 `RedisClient::Shutdown()`，Redis 状态变为 disabled；未完成请求按 `kShutdown` 完成。
- `server.redis` 变为空且 `redis_required = true`：reload 校验失败，保留旧配置和旧 RedisClient 状态。
- `server.redis` 路径变化且新配置加载成功：先按新配置启动新的 `RedisClientThread` worker 集合，成功后再原子替换旧集合并关闭旧集合；如果新集合启动失败，`redis_required = true` 时 reload 失败并保留旧集合和旧配置；`redis_required = false` 且旧集合存在时保留旧集合继续服务并记录 reload error，旧集合不存在时进入 error/disabled 状态。
- host、port、password、database 变化：需要重连；password 的 change old/new 值必须 redacted。
- username 变化：需要重新认证，第一版按重连处理。
- timeout、queue、dispatch batch 变化：可以运行时更新。
- `thread_count` 变化：需要重建 `RedisClientThread` worker 集合；重建同样必须先启动新集合、再替换旧集合，失败时按 `redis_required` 规则回滚或降级。
- `main_loop_fps` 变化：可以更新所有 worker 的 tick event interval；如果实现复杂，第一版也可以按 worker 集合重启处理。
- reconnect delay 和 `queue_while_disconnected` 变化：可以运行时更新。
- script dir 或 auto_load 变化：RedisClientScriptVM 需要重新加载脚本，或标记为下次重启生效。第一版建议“下次重启生效”，避免运行时卸载脚本的生命周期风险。
- `redis_required` 变化：影响 health/readiness，不强制重启 `RedisClientThread` worker 集合；如果变为 true 但 `server.redis` 为空，reload 必须失败。

### 10.6 Lua 配置访问

更新 `src/runtime/config/bind/config_bind.cc`，允许脚本读取 Redis 配置入口字段：

```text
config.get("server.redis")
config.get("server.redis_required")
```

Redis 详细配置不建议默认全部暴露给普通业务脚本，尤其是 password。若确实需要暴露，只开放非敏感字段：

```text
config.get("redis.connection.host")
config.get("redis.connection.port")
config.get("redis.connection.username")
config.get("redis.connection.database")
config.get("redis.queue.dispatch_batch_size")
config.get("redis.thread.thread_count")
config.get("redis.thread.main_loop_fps")
config.get("redis.log.slow_command_ms")
```

禁止通过 Lua config binding 返回：

```text
redis.connection.password
```

### 10.7 配置校验

- Redis runtime 未编译且 `server.redis_required = true` 时，配置无效；Redis runtime 未编译且 `server.redis_required = false` 时，不校验 Redis 详细配置文件是否存在。
- `server.redis_required = true` 时，`server.redis` 必须非空。
- 以下 Redis 详细字段只在 Redis runtime 已编译且 `server.redis` 非空、需要加载 Redis 详细配置时校验。
- host 不能为空。
- port 必须在 1 到 65535。
- username 可以为空；非空时必须随 AUTH username password 形式认证。
- database 必须大于等于 0。
- connect timeout 和 command timeout 必须大于 0。
- request queue size 必须大于 0。
- max inflight 必须大于 0。
- request queue size 和 max inflight 独立校验，不强制大小关系；需要在文档和日志中说明二者共同决定故障时内存上限。
- dispatch batch size 必须大于 0。
- thread count 必须大于 0，建议限制在 1 到 64；默认 1，允许配置为多个 RedisClientThread，但避免误配导致大量线程和 Redis 连接。
- main loop fps 必须大于 0，建议限制在 1 到 240；默认 60，避免配置过高导致 Redis 线程空转。tick interval 应用微秒或更高精度计算，不能用整数毫秒截断。
- reconnect delay 必须大于 0。
- reconnect max delay 必须大于等于 initial delay。
- reconnect backoff multiplier 必须大于等于 1。
- `queue_while_disconnected = true` 时必须同时配置严格的 queue size、max inflight 和 command timeout，避免 Redis 长时间不可用时积压无界请求。
- slow command 阈值必须大于等于 0，0 表示记录所有命令。
- password 不允许出现在日志、health、metrics 和 Lua config 输出中。

校验位置：

- `src/runtime/config/config_validator.cc`：校验 `ServerConfig.redis`、`redis_required` 和 Redis config 基础字段。
- `RedisClientConfig::Validate` 或独立 `ValidateRedisClientConfig`：校验 Redis 详细配置。

### 10.8 默认值和兼容性

默认建议：

- server 默认配置文件中保留 `redis` 为空，Redis 默认不启动。
- 如果项目环境没有 Redis，server 仍可启动。
- `ENGINE_REDIS_ENABLED=OFF` 且 `redis_required = false` 时，`server.redis` 字段可以保留；server 不启动 Redis 模块，也不加载该路径指向的 Redis 详细配置文件，避免旧配置中保留路径但目标文件缺失时影响启动。
- `ENGINE_REDIS_ENABLED=OFF` 且 `redis_required = true` 时，server 启动必须失败或配置校验失败，因为强依赖无法满足。
- client/mobile 不解析 Redis 详细配置。

这样可以保持现有配置文件兼容，同时便于 server 环境逐步启用 Redis。

### 10.9 字段命名和序列化

Redis 配置 JSON 建议全部使用 snake_case，与当前 `ServerConfig`、`db_service.json` 风格保持一致。

如果 C++ 字段名与 JSON 字段名完全一致，可以依赖现有 glz 反射方式；如果后续出现 camelCase 或兼容旧字段，再为 `RedisClientConfig` 增加显式 `glz::meta` 映射。

敏感字段处理：

- `password` 只参与连接，不进入 `ConfigChange` 的明文 old/new 值。
- reload 日志中只记录 password 是否变化，例如 `<redacted:changed>`，不记录变化前后的值。
- metrics、health、admin API、Lua config binding 均不能返回 password。

## 11. CMake 集成方案

新增 server-only 开关：

```cmake
option(ENGINE_REDIS_ENABLED "Enable Redis runtime module" ON)
```

server target 中：

- 在 `include(${ENGINE_ROOT}/runtime/CMakeLists.txt)` 之前设置 `ENGINE_REDIS_RUNTIME_ENABLED`，因为 runtime CMake 会根据该变量决定是否把 Redis 源文件加入 `CLOUD_ENGINE_SOURCES`。
- 添加 hiredis subdirectory。
- 关闭 hiredis tests/examples/SSL。
- 链接 `hiredis::hiredis` 和 libevent。
- 添加 `ENGINE_REDIS_ENABLED` compile definition。

runtime CMake 中：

- `redis_config.h/.cc` 加入 `CONFIG_SOURCES`，无条件编译，因为它不依赖 hiredis/libevent，也需要被 `ConfigManager` 使用。
- `async_result_dispatcher.h/.cc` 加入 `VM_SOURCES`，无条件编译，因为多个 VM owner 都会复用。
- Redis runtime 源码单独定义 `REDIS_SOURCES`，不要混进 `CONFIG_SOURCES`。
- 只根据 `ENGINE_REDIS_RUNTIME_ENABLED` 且非 mobile 追加 Redis 源码。
- client/mobile 必须显式不编译 Redis 源码。
- 避免因为 CMake cache 导致 client 误继承 server 的 Redis 开关。

server CMake 建议在 include runtime 之前：

```cmake
set(ENGINE_REDIS_RUNTIME_ENABLED OFF)
if(ENGINE_REDIS_ENABLED AND
   NOT (CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  set(ENGINE_REDIS_RUNTIME_ENABLED ON)
endif()
```

runtime CMake 建议：

```cmake
if(ENGINE_REDIS_RUNTIME_ENABLED AND
   NOT (CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  list(APPEND CLOUD_ENGINE_SOURCES ${REDIS_SOURCES})
endif()
```

client CMake 建议显式禁用 Redis runtime，且不要声明同名 `ENGINE_REDIS_ENABLED` cache option，避免复用同一构建树时继承 server 的 cache 值：

```cmake
set(ENGINE_REDIS_RUNTIME_ENABLED OFF)
```

hiredis 接入建议：

```cmake
if(ENGINE_REDIS_RUNTIME_ENABLED AND NOT TARGET hiredis::hiredis)
  block(SCOPE_FOR VARIABLES)
    set(BUILD_SHARED_LIBS OFF)
    set(DISABLE_TESTS ON)
    set(ENABLE_EXAMPLES OFF)
    set(ENABLE_SSL OFF)
    set(ENABLE_SSL_TESTS OFF)
    set(ENABLE_ASYNC_TESTS OFF)
    set(ENABLE_NUGET OFF)
    add_subdirectory(${ENGINE_ROOT}/thirdparty/hiredis
                     ${CMAKE_BINARY_DIR}/hiredis EXCLUDE_FROM_ALL)
  endblock()
endif()
```

include/link：

- `ENGINE_RUNTIME_INCLUDE_DIRS` 增加 `${ENGINE_ROOT}/thirdparty/hiredis`，用于包含 `hiredis.h`、`async.h` 和 `adapters/libevent.h`。
- `ENGINE_RUNTIME_LINK_LIBS` 增加 `hiredis::hiredis`。
- Windows 下 hiredis target 会带上 `ws2_32`、`crypt32`，server target 仍可保留现有 `ws2_32` 链接。

注意：

- 当前工程 `cmake_minimum_required(VERSION 4.0)`，hiredis 的 `option()` 会遵守同名普通变量；优先在 `block(SCOPE_FOR VARIABLES)` 中设置普通变量，不要用 `CACHE FORCE` 污染父作用域。
- `BUILD_SHARED_LIBS`、`DISABLE_TESTS`、`ENABLE_EXAMPLES`、`ENABLE_SSL`、`ENABLE_SSL_TESTS`、`ENABLE_ASYNC_TESTS`、`ENABLE_NUGET` 都是 hiredis 使用的选项名。如果后续必须使用 cache 变量，应保存/恢复这些变量，或把 hiredis 配置封装成 helper 函数。
- `ENGINE_REDIS_ENABLED` compile definition 只应在 `ENGINE_REDIS_RUNTIME_ENABLED` 为 true 时加到 server target；client target 不应看到该宏。
- 配置层可用 `#if defined(ENGINE_REDIS_ENABLED)` 或一个无 Redis 头依赖的 helper 判断当前 target 是否编译了 Redis runtime；不能通过 `server.redis` 字段是否非空来推断 Redis runtime 可用。
- client/mobile target 不应依赖 `ENGINE_REDIS_ENABLED` cache option 的值；它们必须在 include runtime 前把 `ENGINE_REDIS_RUNTIME_ENABLED` 设为 OFF，并且不添加 `ENGINE_REDIS_ENABLED` compile definition。

## 12. 引擎生命周期

### 12.1 初始化

建议顺序：

1. 加载 server config。
2. 初始化 logger。
3. 初始化主 EventLoop。
4. 如果 Redis runtime 已编译且 `server.redis` 非空，加载 Redis config；Redis runtime 未编译时不加载 Redis 详细配置。
5. 如果 Redis config 加载成功，初始化 `RedisClient`；如果 `server.redis` 为空、Redis runtime 未编译且 `redis_required = false`，或 Redis config 加载失败但 `redis_required = false`，保持 Redis 状态为 disabled/error，不创建 worker 集合。
6. 初始化 Mongo / DatabaseService。
7. 创建主 `MainThreadScriptVM`。
8. 导出 runtime bindings；Redis runtime 编译启用时，包含访问 `RedisClient` 的 Redis Lua binding。

Redis 启用时放在 Mongo / DatabaseService 之前初始化，是为了让后续 DB 线程、业务线程或脚本 VM 在创建时可以安全导出访问 `RedisClient` 的 Redis Lua binding。Engine 读取 `server.redis_required` 后，转成 `RedisClientStartOptions::wait_for_initial_connect` 传给 `RedisClient::Initialize`：`redis_required = true` 时必须等待所有 `RedisClientThread` worker 的首次连接结果；`redis_required = false` 时只要求所有 Redis worker 基础设施启动成功。`server.redis` 为空且 `redis_required = false` 时不调用 `RedisClient::Initialize`；`server.redis` 为空但 `redis_required = true` 必须在配置校验阶段失败。Redis runtime 未编译且 `redis_required = false` 时也不调用 `RedisClient::Initialize`，即使 `server.redis` 保留了旧路径。

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

Redis 应在所有可能拥有 Lua VM/dispatcher 的模块销毁前停止。建议在 Engine cleanup 中新增 `RedisShutdown` 阶段，并把它放在 `PhysicsShutdown`、`DatabaseShutdown`、`NetworkShutdown`、`TimerShutdown` 和 `DestroyScript` 之前：

```text
RedisClient::Shutdown
PhysicsEngineBridge::Shutdown
DatabaseService::Shutdown
MongoSystem::Shutdown
ShutdownNetworkBindings
ShutdownTimerBindings
DestroyScript / destroy VMs
final logs
```

原因：

- 需要同步更新 `Engine::CleanupPhase` 枚举和 `engine.h` 中的 cleanup 顺序注释，避免监控日志和生命周期文档遗漏 Redis 阶段。
- Redis pending completion 可能投递到主 VM、Space VM、DB VM、Physics VM 或 Redis 私有 VM 的 dispatcher。
- 如果先销毁任一 VM owner，再让 Redis 线程返回结果，会造成 dispatcher 失效或 Lua ref 悬挂。
- Redis shutdown 应先停止接收新请求，再让每个 `RedisClientThread` worker 把 pending 请求转成 `kShutdown`，最后唤醒目标 dispatcher；目标 dispatcher 是否执行 `kShutdown` 对应的 Lua/业务回调，由 owner thread 在本模块 shutdown 期间决定。
- VM 销毁前必须调用对应 dispatcher 的 `ShutdownOnOwnerThread`，释放未完成 Lua callback ref。

## 13. 错误处理和重连策略

### 13.1 命令失败

Redis 命令错误，例如 `WRONGTYPE`，应作为 `kCommandError` 返回；已 accepted 请求的 completion 必须执行，Lua/业务回调再由 dispatcher 派发。

### 13.2 连接失败

连接失败时：

- `RedisClient::IsHealthy()` 返回 false。
- 如果 `redis_required = true`，server 初始化可以失败，readiness 必须失败。
- 如果 `redis_required = false`，server 可以继续启动；在 `RedisClient` 已初始化且 worker 集合存在的前提下，全部 worker 不可用时 Redis 请求返回 connection error，部分 worker 不可用时按路由规则处理。Redis runtime 未编译、`server.redis` 为空或 Redis config 加载失败导致没有 worker 集合时，提交请求同步返回 `kNotRunning`。
- 单个 worker 连接断开时，该 worker 所有已发送但未完成的 pending request 应立即完成为 `kConnectionError`，不要静默等待重连，避免非幂等命令在不确定状态下重复执行。
- 使用 5.1 中定义的 `RedisClientState`：所有 worker healthy 时为 `kHealthy`，部分 worker unhealthy 时为 `kDegraded`，全部 worker 不可用或启动失败时进入 `kError` / `kReconnecting` / `kStopped` 等对应状态。

readiness 规则：

- Redis runtime 未编译且 `redis_required = false`：Redis 状态为 disabled，即使 `server.redis` 非空也不影响 readiness。
- Redis runtime 未编译且 `redis_required = true`：配置无效，server 不应进入 ready 流程。
- `server.redis` 为空且 `redis_required = false`：Redis 状态为 disabled，不影响 readiness。
- `server.redis` 为空且 `redis_required = true`：配置无效，server 不应进入 ready 流程。
- `server.redis` 非空、Redis config 加载失败且 `redis_required = false`：Redis 状态为 error，不创建 worker，不导致整体 readiness fail。
- `server.redis` 非空、RedisClient 已初始化且 `redis_required = false`：Redis unhealthy 只标记 degraded，不导致整体 readiness fail。
- `server.redis` 非空且 `redis_required = true`：Redis 未 healthy 时整体 readiness fail。

### 13.3 超时

每个 pending request 记录 deadline。

每个 Redis 线程通过自己的定时 event 检查本 worker 的超时：

- 超时请求从 pending map 删除。
- completion 返回 `kTimeout`。
- 如果 hiredis 后续又返回该 request，必须识别为已完成并丢弃。
- hiredis callback 的 `privdata` 不能指向会在超时时释放的 `PendingRedisRequest`。建议传递稳定 token，例如由 hiredis callback 自己释放的 heap token，或 `shared_ptr<PendingToken>`；超时只标记完成，不释放 callback 仍可能访问的 token，不让晚到 callback 发生 use-after-free。

### 13.4 重连

第一版建议支持简单重连：

- 每个 worker 的 disconnect callback 只把该 worker 设置为 healthy false。
- 该 worker 的所有 pending request 立即完成为 `kConnectionError`。
- 该 worker 定时重连。
- 该 worker 重连成功后 healthy true，并更新 `RedisClient` 聚合 health/state。
- 断线期间新请求默认不进入 unhealthy worker 队列，`RedisClient::Command` / `Eval` 返回 `accepted = false`，Lua binding 同步返回错误；未指定 `routing_key` 的请求可以继续路由到其他 healthy worker。
- 只有 `queue_while_disconnected = true` 时，断线 worker 才允许接收新请求入队；入队仍必须受 request queue size、max inflight 和 command timeout 限制。
- command timeout 从请求 accepted 时开始计算，包含断线排队等待重连的时间；超时后即使尚未发送到 Redis，也必须完成为 `kTimeout`。

默认不排队的原因：

- Redis 命令可能不是幂等操作。
- 断线前已发送命令的执行结果可能未知。
- 排队等待重连会放大故障期间内存压力和恢复瞬间的流量尖峰。

## 14. 不建议第一版支持的能力

以下能力会扩大复杂度，建议第二阶段再做：

- Redis Cluster。
- Sentinel。
- Pub/Sub。
- blocking commands，例如 `BLPOP`。
- connection-state commands，例如 `AUTH`、`SELECT`、`CLIENT`、`MONITOR`。
- transaction commands，例如 `MULTI`、`EXEC`、`WATCH`。
- pipeline 批量聚合。
- RESP3 完整类型支持。
- 多 Redis 实例或通用连接池。

原因：

- Pub/Sub 和 blocking command 不适合与普通命令共用 worker async context。
- connection-state 和 transaction command 会修改单个 Redis 连接的状态；在多 worker、多调用方共享连接的模型下容易破坏隔离和顺序语义。
- Cluster/Sentinel 会显著增加连接管理复杂度。
- 多个 `RedisClientThread` worker 仍连接同一份 Redis 配置，由 `RedisClient` 统一管理；这不等同于多 Redis 实例、Cluster、Sentinel 或通用连接池。

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
- `RedisSubmitResult` 在参数非法、completion 为空、未启动、断线、队列满、正在关闭时返回正确同步拒绝状态，且不触发 completion。
- `AUTH`、`SELECT`、Pub/Sub、blocking command、transaction command 等 unsupported command 返回 `kUnsupportedCommand`，不入队、不触发 completion。
- Lua `redis.command` 调用 unsupported command 时同步返回 `false, error`，且不保存 callback ref。
- `Initialize` 失败后清理完整，允许后续重试初始化。
- `Initialize` 运行中重复调用会失败；`Shutdown` 完成后允许重新 `Initialize`，用于 reload 从 disabled 重新启用 Redis。
- `server.redis_required = true` 但 `server.redis` 为空时，配置校验失败并阻止 server 初始化。
- Redis runtime 未编译、`redis_required = false` 且 `server.redis` 保留非空路径时，不加载 Redis 详细配置、不创建 worker，普通 VM 不导出 `redis` 表。
- `thread_count > 1` 时 `RedisClient` 能启动多个 `RedisClientThread`，任一 worker 启动失败会清理已启动 worker。
- `wait_for_initial_connect = true` 且多 worker 时，首次连接等待使用整体 `connect_timeout_ms` 预算，不按 worker 数串行累加。
- 多个 worker 的 OS thread name 仍设置为 `RedisClientThread`，worker index 通过 stats/log/profiler context 暴露。
- `main_loop_fps` 默认值为 60，tick interval 使用微秒或更高精度计算；配置变化能正确更新 tick interval 或触发 worker 集合重启。
- `routing_key` 相同的请求稳定进入同一个 worker；`Command` 未指定 `routing_key` 时按 round-robin 分配。
- 普通外部 `Eval` / Lua `redis.eval` 未显式设置 `routing_key` 且 keys 非空时，默认用第一个 key 路由。
- `thread_count` reload 后，新的 worker generation 可以改变 `routing_key` 映射，但旧 generation 已 accepted 请求仍必须完成一次。
- 部分 worker unhealthy 时，未指定 `routing_key` 的请求只路由到 healthy worker；指定 `routing_key` 命中 unhealthy worker 时不改投其他 worker。
- 全部 worker unhealthy 且 `queue_while_disconnected = true` 时，未指定 `routing_key` 的请求可以在 running worker 中 round-robin 排队；没有 running worker 时同步失败。
- Redis 私有 VM 未显式指定 `routing_key` 发起请求时，使用内部 `preferred_worker_index` 固定回所属 worker；Lua `redis.eval` 带 keys 但未显式指定 `routing_key` 时，也不能用默认首 key 覆盖 `preferred_worker_index`。
- Redis runtime 已编译但未在 server 配置中启用时，不创建 worker 集合；Lua `redis.is_running()` / `redis.is_healthy()` 返回 false，`redis.command` 同步失败且不保存 callback ref。
- 部分 worker unhealthy 时，Lua `redis.is_running()` 返回 true、`redis.is_healthy()` 返回 false。
- request id 由 `Command` / `Eval` 返回，异步 `RedisResult.request_id` 与提交结果一致。
- request queue 和 max inflight 由显式计数器强制限制，不依赖 `ConcurrentQueue` 自身容量，并且二者大小关系互不限制。
- `AsyncResultDispatcher` 跨线程 enqueue，同线程 dispatch。
- dispatcher shutdown 后拒绝投递。
- Lua callback registry ref 生命周期，确认 `luaL_unref` 只在 owner thread 执行。
- VM 销毁后结果不再回调 Lua，晚到 Redis result 只被丢弃并记录日志。
- accepted 请求在 shutdown、timeout、connection error 下只完成一次。
- timed out 请求的 hiredis late callback 不触发 use-after-free。
- `queue_while_disconnected = false` 时断线请求同步失败；`true` 时按上限入队。
- `queue_while_disconnected = true` 时，断线排队请求在 worker 重新 healthy 前不会发送给 hiredis；排队期间超时完成为 `kTimeout`，shutdown 完成为 `kShutdown`。
- reload 时 `server.redis` 空/非空切换能正确启动或关闭 RedisClient，失败时按 `redis_required` 决定 reload 成败。
- reload 把 `server.redis` 置空且 `redis_required = true` 时必须失败并保留旧配置。
- reload 重建 worker 集合时先启动新集合再原子替换旧集合；新集合启动失败时，`redis_required = true` 保留旧集合并判定 reload 失败，`redis_required = false` 优先保留旧集合继续服务，只有旧集合不存在时才降级到 error/disabled。

### 15.2 集成测试

Redis 集成测试建议 opt-in：

- 环境变量 `ENGINE_REDIS_TEST_URL` 或 `ENGINE_REDIS_TEST_ENABLED=1`。
- 本地 Redis 可用时测试 `PING`、`SET`、`GET`、错误命令、超时、断线。
- Redis 6+ 可用时测试 username/password AUTH；否则至少测试 password-only AUTH。
- 配置 `database > 0` 时测试 `SELECT` 生效，并确认 AUTH/SELECT 完成前 `IsHealthy()` 不返回 true。
- 默认 CI 如果没有 Redis，不运行集成测试。

### 15.3 编译测试

必须覆盖：

- server target 编译包含 Redis。
- client target 不编译 Redis。
- mobile target 不编译 Redis。
- `ENGINE_REDIS_ENABLED=OFF` 时 server 可正常编译。
- 同一构建树先配置 server 再配置 client 时，client 仍不继承 `ENGINE_REDIS_RUNTIME_ENABLED`。
- `ENGINE_REDIS_ENABLED=ON`、`server.redis` 为空且 `redis_required = false` 时，server 编译和启动均不强制连接 Redis。
- `ENGINE_REDIS_ENABLED=OFF`、`server.redis` 非空且 `redis_required = false` 时，server 启动不尝试加载 Redis 详细配置文件。
- `ENGINE_REDIS_ENABLED=OFF` 且 `redis_required = true` 时，server 启动或配置校验失败。

## 16. 实施步骤

### 阶段一：工程骨架

1. 增加 `ENGINE_REDIS_ENABLED`。
2. 在 server/client CMake 中设置 `ENGINE_REDIS_RUNTIME_ENABLED`，并确认 runtime 源列表判断发生在 include runtime 前。
3. server CMake 接入 hiredis。
4. 新增 Redis 模块目录和空类。
5. 新增 `runtime/config/redis_config.h/.cc`、Redis config 结构和配置文件。
6. 确认 client/mobile 不编译 Redis。

### 阶段二：公共派发基础设施

1. 实现 `AsyncResultDispatcher`。
2. 实现 Lua callback registry token 机制，保证 Lua ref 只在 owner thread 释放。
3. 为主 VM、space VM、DB VM、physics VM 接入 dispatcher。
4. 增加 dispatcher 单元测试。

### 阶段三：Redis 核心线程

1. 实现 `RedisClient`。
2. 实现 `RedisSubmitResult`、`RedisRequest`、`RedisResult`、`RedisValue`。
3. 实现 request queue 和 max inflight 背压计数。
4. 实现 `RedisClientThread`。
5. 实现 `RedisClientThread` worker 集合统一管理、启动失败回滚、snapshot/generation 安全替换、round-robin 路由和 `routing_key` 稳定 hash 路由。
6. 接入 hiredis async 和 libevent adapter。
7. 实现 AUTH/SELECT 连接握手和 health 状态。
8. 实现 wakeup fd、shutdown、timeout、connection error、late callback 去重处理。
9. 实现 Redis 私有 VM tick event，默认 `main_loop_fps = 60`，interval 使用微秒或更高精度计算。

### 阶段四：Lua VM 和 binding

1. 实现 `RedisClientScriptVM`。
2. 通过 `custom_ptr_store` 绑定 Redis 模块对象。
3. 为每个 Redis 私有 VM 注册自己的 dispatcher 和 tick event。
4. 实现 `bind/redis_bind.cc`。
5. Redis runtime 编译启用时，外部 VM 导出访问 `RedisClient` 的 `redis.command`、`redis.eval`、`redis.is_running`、`redis.is_healthy`。
6. Lua callback 结果通过 dispatcher 回到所属线程。

### 阶段五：生命周期和可观测性

1. Engine 初始化 Redis。
2. Engine 关闭 Redis。
3. 增加 `RedisShutdown` cleanup 阶段，保证早于所有 VM owner 销毁。
4. 增加 Redis health、readiness 和 stats。
5. 增加慢命令日志。
6. 增加连接失败和重连日志。

### 阶段六：测试和文档

1. 补单元测试。
2. 补 opt-in 集成测试。
3. 补 Lua 使用示例。
4. 更新 Redis 模块文档。

## 17. 风险和规避

### 17.1 回调线程错误

风险：hiredis callback 在 Redis 线程执行，如果直接调用外部 Lua VM，会造成线程安全问题。

规避：hiredis callback 只完成 Redis 请求并生成 `RedisResult`。低层 `RedisCompletion` 必须线程安全；Lua callback 和非线程安全业务 continuation 必须通过 `AsyncResultDispatcher` 投递回 owner thread。

### 17.2 VM 生命周期

风险：Lua callback ref 所属 VM 已销毁，但 Redis 结果稍后返回。

规避：dispatcher 使用 weak ownership；VM shutdown 时 dispatcher 在 owner thread 标记关闭并释放 callback ref。

### 17.3 请求永不回调

风险：shutdown、超时、连接断开时 pending request 丢失。

规避：所有 accepted request 必须进入 pending 管理；关闭和超时统一生成失败结果。

### 17.4 晚到 reply 二次完成

风险：请求已超时或断线失败后，hiredis late callback 又返回同一 request，导致二次回调或访问已释放 pending 对象。

规避：pending token 必须稳定存在到 hiredis callback 结束；request 完成状态使用原子或 Redis 线程内状态机保护，late callback 只能记录并丢弃。

### 17.5 client 误编译 Redis

风险：CMake cache 导致 client 继承 server 的 `ENGINE_REDIS_ENABLED`。

规避：使用 target-specific 变量或在 client/mobile 明确禁用 Redis runtime。

### 17.6 多 worker 路由和顺序风险

风险：多个 `RedisClientThread` 意味着多个 Redis 连接；不同 worker 上的命令没有全局顺序，同一业务对象的连续操作如果被路由到不同 worker，可能出现业务层观察到的顺序问题。

规避：`RedisClient` 必须统一管理路由策略；提供 `routing_key`，保证相同 key 稳定进入同一个 worker。文档和 Lua binding 都要说明未指定 `routing_key` 时不保证跨请求顺序；需要严格顺序的调用方应串行提交或使用相同 `routing_key`。

### 17.7 worker 集合 reload 竞态

风险：reload 重建 `RedisClientThread` worker 集合时，请求路由可能拿到旧集合指针；如果旧集合同时关闭，会出现 accepted 请求被错误丢弃、重复完成或访问已销毁 worker。

规避：worker 集合应通过 `std::shared_ptr` 快照或 generation id 管理。`Command` / `Eval` 在路由前获取当前集合快照；reload 先启动新集合，原子发布新快照，再关闭旧集合。旧集合关闭时仍必须把已经 accepted 的请求完成为正常结果或 `kShutdown`，不能静默丢弃。

## 18. 推荐第一版交付边界

第一版交付以下能力：

- server-only 编译开关。
- RedisClient 公共单例。
- `RedisClient` 统一管理一个或多个独立 `RedisClientThread` worker。
- 每个 `RedisClientThread` 主循环默认 60 fps。
- hiredis async + libevent。
- RedisClientScriptVM。
- Redis runtime 编译启用时的 Lua `redis.command`、`redis.eval`、`redis.is_running`、`redis.is_healthy`。
- 跨线程 dispatcher。
- 请求成功、命令失败、连接失败、超时、关闭失败 completion。
- Lua/业务回调按 owner thread dispatcher 派发。
- 基础配置、日志、health、readiness、stats。
- 单元测试和可选 Redis 集成测试。

第一版不交付：

- Cluster。
- Sentinel。
- Pub/Sub。
- blocking command。
- connection-state command。
- transaction command。
- 多 Redis 实例或通用连接池。
- pipeline 聚合优化。
