# Runtime 深度审查报告

审查日期：2026-05-31  
审查范围：`src/runtime` 全目录，重点覆盖 engine 生命周期、evpp 网络、Lua VM/脚本绑定、entity/space、config、database、physics、monitoring、auth/rpc。  
代码基线：`4b559bcc Harden runtime config loading and validation`

## 总体结论

`src/runtime` 已经具备较完整的游戏服务器运行时能力：主循环、TCP/UDP/KCP、Lua VM、热更新、配置、MongoDB 异步服务、物理线程、实体/空间和基础监控都有实现。但当前架构仍处于“功能聚合型 runtime”阶段，主要风险集中在四类问题：

1. 生命周期和线程归属没有被统一建模，多个模块依赖隐含主线程约定。
2. 运维可观测性字段已经存在，但核心指标没有实际埋点，导致健康检查和停服排水依据不可靠。
3. Lua 绑定层和全局单例较多，跨 VM、跨线程、重初始化时容易出现悬挂回调、重复回调或状态污染。
4. 认证、脚本执行和管理端口存在生产安全硬伤，需要在对公网或半可信环境上线前优先整改。

## 架构概览

当前 runtime 可以概括为：

- `engine`：总入口，初始化 logger、profiler、timer、event loop、MongoDB、physics、ScriptVM、hot reload、admin HTTP，并驱动每帧更新。
- `evpp`：libevent 封装及 TCP/UDP/KCP/HTTP 基础设施。
- `script` + `vm`：Lua VM、import、热更新，以及网络、实体、RPC、配置等 Lua 绑定。
- `entity` + `space`：全局实体管理器、多 Space 隔离 VM、连接路由。
- `database`：Mongo C/C++ wrapper、异步 DBService、DB 专用 Lua VM、ORM/cache。
- `physics`：独立物理线程、命令/结果队列、物理 VM。
- `monitoring`：metrics registry 和 admin HTTP 健康检查。
- `config`：配置加载、校验、热更新和 Lua 访问。

## P0：必须优先处理

### P0-1 监控指标几乎没有接入真实事件，停服排水和 /stats 数据不可信

证据：

- `Engine::Cleanup()` 依赖 `connections_active()` 判断是否需要等待连接排水：`src/runtime/engine/engine.cc:599`、`src/runtime/engine/engine.cc:608`。
- `MetricsRegistry` 定义了连接、消息、timer、DB 指标访问器：`src/runtime/monitoring/metrics.cc:160`、`src/runtime/monitoring/metrics.cc:178`。
- 全 runtime 中这些指标基本只被读取或注册，没有在 TCP accept/close、send/recv、timer fire、DB request 等关键路径更新。

影响：

- 停服排水可能直接跳过，活跃连接仍在处理时就进入网络/脚本 teardown。
- `/stats`、`/metrics` 给出的连接数、消息数、DB 请求数长期为 0 或不完整，运维无法基于它们做容量判断和告警。
- `shutdown_timeout_sec`、`connection_drain_timeout_sec` 等配置看似可用，实际缺少可信数据源。

建议：

- 在 `TCPServer::HandleNewConn` / `RemoveConnection`、`TCPConn::HandleRead` / `SendInLoop`、Lua net bindings、DBService `SendRequest`、TimerManager fire 点统一埋点。
- 把指标更新封装成小型 adapter，避免业务层直接散落 `MetricsRegistry::Instance()`。
- 增加连接排水集成测试：建立连接、触发 shutdown、验证 drain 阶段确实等待连接关闭或超时。

### P0-2 健康检查把可选数据库当成强依赖，可能让无 MongoDB 服永远 NotReady

证据：

- Engine 初始化时，如果 Mongo URI 为空会跳过 DBService：`src/runtime/engine/engine.cc:217`。
- readiness 检查中，只要编译了 MongoDB 且 `DatabaseService::Instance().IsRunning()` 为 false，就返回 DB error：`src/runtime/monitoring/admin_http.cc:124`。

影响：

- 对于不依赖 MongoDB 的游戏服、无状态网关、压测服、纯逻辑服，进程初始化成功后 `/health/readiness` 仍会 503。
- Kubernetes / SLB 会持续摘除健康实例，导致运维误判为启动失败。

建议：

- 增加明确的 `server.db_required` 或 `db_service.enabled` 配置。
- 当 Mongo URI 为空且 DB 非必需时，readiness 返回 `disabled` 或 `ok`，而不是 `error`。
- 健康检查应区分 `required dependency` 和 `optional dependency`。

### P0-3 认证实现不满足生产安全要求

证据：

- Session ID 使用 `std::mt19937` 生成：`src/runtime/auth/session_manager.cc:35`、`src/runtime/auth/auth_backend.cc:365`。
- JWT 在 `secret_` 为空时跳过签名验证：`src/runtime/auth/auth_backend.cc:264`。
- JWT 只用字符串搜索解析 `sub` / `exp`，没有校验 `alg`、`iss`、`aud`、`nbf`，签名比较也不是常量时间：`src/runtime/auth/auth_backend.cc:252`。

影响：

- `mt19937` 不是 CSPRNG，会话 ID 不应作为生产鉴权 token。
- 如果配置漏填 JWT secret，任何结构正确的 token 都可能被接受。
- JWT 解析绕过、签名侧信道、claim 缺失都会扩大账号冒用风险。

建议：

- Session/token 改用 OS CSPRNG：Windows `BCryptGenRandom`，Linux `getrandom` / libsodium。
- JWT secret 为空必须 fail closed。
- 使用成熟 JWT 库或至少补齐 alg 白名单、常量时间比较、issuer/audience/nbf/exp 校验和 JSON 解析。
- 增加安全单测：空 secret、篡改签名、alg none、过期、未来 nbf、错误 issuer/audience。

### P0-4 Engine 初始化对关键失败过于宽松，可能以半初始化状态对外服务

证据：

- DBService 初始化失败只记录 `ok`，Engine 继续：`src/runtime/engine/engine.cc:227`。
- Physics 初始化失败只 warn：`src/runtime/engine/engine.cc:258`。
- 脚本目录加载失败后仍调用 `InitScript()`：`src/runtime/engine/engine.cc:310`、`src/runtime/engine/engine.cc:315`。
- `Start()` 无条件启动物理和 frame timer，并设置 running：`src/runtime/engine/engine.cc:374`、`src/runtime/engine/engine.cc:413`、`src/runtime/engine/engine.cc:417`。

影响：

- 关键脚本缺失、DB 不可用、物理不可用时，进程仍可能暴露 TCP/HTTP/health，造成“活着但不可服务”的实例。
- 故障从启动期转移到运行期，定位成本更高。

建议：

- 引入 `SubsystemRequirement`：required、optional、disabled。
- `Engine::Init()` 返回 `bool` 或抛出明确异常；`Run()` 只在 required 子系统全部 ready 后进入服务。
- readiness 直接引用初始化结果，而不是重新推断。

## P1：高优先级架构问题

### P1-1 Engine reload callback 没有保存并注销，重复 Init 或测试注入会累积悬挂回调

证据：

- `Engine::Init()` 每次注册 reload callback，捕获 `this`：`src/runtime/engine/engine.cc:336`。
- `ConfigManager::RegisterReloadCallback()` 返回 callback id：`src/runtime/config/config.cc:997`。
- Engine 没有保存该 id，也没有在 `Cleanup()` 注销。

影响：

- 同一个 Engine 多次 Init 会收到重复配置变更。
- `SetInstanceForTesting()` 场景下，测试实例销毁后 ConfigManager 仍可能持有旧 `this`。
- 未来如果 Engine 不再是进程单例，会形成确定的 use-after-free 风险。

建议：

- Engine 成员保存 `config_reload_callback_id_`，Cleanup 中调用 `UnregisterReloadCallback()`。
- 注册前先注销旧 id，保证 Init 幂等。

### P1-2 ScriptReloader 可能留下已排队的 lambda 捕获失效 this

证据：

- `OnFilesChanged()` 从 watcher 线程向 event loop 投递 `[this, files]`：`src/runtime/vm/script_reloader.cc:151`。
- `ScriptReloader::Stop()` 只停止 watcher 并 reset，没有取消 event loop 中已排队任务：`src/runtime/vm/script_reloader.cc:93`。

影响：

- Cleanup 或热重启时，`script_reloader_` reset 后，event loop 若继续处理旧任务，会访问已释放对象。
- library mode 下外部 loop 仍在运行，此风险更明显。

建议：

- ScriptReloader 使用 `enable_shared_from_this` 或 generation token；排队任务执行前校验 token。
- Stop 时标记 `stopped_`，所有 lambda 只捕获 `weak_ptr` 或 immutable snapshot。

### P1-3 EntityManager / SpaceManager 缺少线程模型强约束

证据：

- `EntityManager` 持有 `entities_`、连接索引、物理索引，但没有 mutex 或线程断言：`src/runtime/entity/entity_manager.h:64`。
- `SpaceManager` 持有 `spaces_`，只有 id allocator 是 atomic：`src/runtime/space/space_manager.h:46`。
- `ConnectionRouter` 只保护连接到 space/entity 的映射，但随后直接访问 `SpaceManager` 和 Lua state：`src/runtime/space/connection_router.cc:68`。

影响：

- 只要网络回调、RPC 回调、脚本热更新、物理回调不在同一主线程，entity/space 容器就有数据竞争。
- 当前注释没有明确“只能主线程调用”，也没有 `VerifyMainThread()` 之类的运行时保护。

建议：

- 对 EntityManager / SpaceManager 明确二选一：主线程独占并加 thread verifier，或内部加锁并定义回调不得持锁。
- 所有从网络线程进入 entity/space/Lua 的路径必须先 `EventLoop::QueueInLoop()` 到所属 VM 线程。

### P1-4 Space 内 Entity 的 Timer 语义不完整

证据：

- `Space::CreateEntity()` 创建 entity 后没有注入 Space 或 TimerManager：`src/runtime/space/space.cc:70`。
- `Entity::AddTimer()` 的安全回调总是通过全局 `EntityManager::Instance().GetEntity(eid)` 找实体：`src/runtime/entity/entity.cc:139`。

影响：

- Space 内实体调用 Entity timer API 时，回调会去全局 EntityManager 查实体，找不到则不触发业务回调。
- 多 Space 架构下，全局 entity 与 space entity 的身份域混在一起，后续 AOI、物理、连接绑定都容易出现归属不清。

建议：

- Entity 增加 owner/context：global manager 或 space manager。
- Timer callback 绑定弱引用式 owner lookup，不要硬编码全局 EntityManager。
- Space 创建时注入 timer manager 或提供 Space-local timer service。

### P1-5 Lua TCP server binding 依赖全局裸指针表，跨 VM/重入/关闭时风险高

证据：

- 全局 `g_server_ctxs`、`g_conn_shared`、`g_server_shared` 无 mutex：`src/runtime/script/net_tcp_server_bind.cc:60`、`src/runtime/script/net_tcp_server_bind.cc:67`。
- callbacks 捕获 `lua_State*`、registry ref 和 `ServerCtx*`：`src/runtime/script/net_tcp_server_bind.cc:354`、`src/runtime/script/net_tcp_server_bind.cc:441`。
- `ShutdownServerBindings()` 直接遍历全局 context 并调用 `ctx->server->Stop()`：`src/runtime/script/net_tcp_server_bind.cc:536`。

影响：

- 当前 Lua server 强制 `thread_num=0`：`src/runtime/script/net_tcp_server_bind.cc:335`，降低了回调跨线程概率，但全局表仍跨 VM 共享。
- 多 Lua VM、多 Space 或未来允许 worker loop 后，Lua C API 可能从错误线程调用。
- GC、手动 stop、Engine cleanup、连接 close 的重入路径复杂，容易引入 double unref 或延迟释放访问旧 ctx。

建议：

- 把 net binding context 绑定到 `ScriptVM` 生命周期，不使用进程级全局容器。
- 所有 Lua 回调统一投递到 VM 所属 event loop。
- ServerCtx/ConnCtx 使用 shared ownership + generation id，Lua userdata 只存 weak handle。

### P1-6 EventLoop 任务队列缺少容量和降级策略

证据：

- `QueueInLoop()` 每次递增 `pending_functor_count_` 后无上限入队：`src/runtime/evpp/event_loop.cc:296`。
- boost / concurrentqueue 分支使用空 while 重试：`src/runtime/evpp/event_loop.cc:306`、`src/runtime/evpp/event_loop.cc:350`。

影响：

- 热更新、网络回调、RPC 回调或外部线程暴增投递任务时，可能导致内存无限增长或 CPU 忙等。
- 没有 per-source 统计，运维无法知道是谁把 loop 打爆。

建议：

- 引入 `max_pending_functors` 配置和返回值，支持 reject / drop-low-priority / backpressure。
- 暴露 pending queue metrics 和日志限频告警。

### P1-7 ORM 的异步写入失败被静默吞掉

证据：

- `SendBestEffort()` 直接调用 `DatabaseService::Instance().SendRequest()`，忽略返回值：`src/runtime/database/orm.cc:224`。
- Insert/Update/Delete 在更新本地缓存后调用 best-effort 发送：`src/runtime/database/orm.cc:384`、`src/runtime/database/orm.cc:427`、`src/runtime/database/orm.cc:454`。

影响：

- DBService 未运行、队列满、线程异常时，本地缓存已经显示成功，但持久化请求可能没有进入队列。
- 进程重启后数据丢失，且调用方拿不到错误。

建议：

- 写操作返回明确状态：local_applied、queued、persisted/failed。
- 至少在 SendRequest false 时回滚本地缓存或标记 dirty，并暴露重试队列。

### P1-8 DB 响应队列满时丢最旧响应，会让等待方永久等不到结果

证据：

- `DBThread::EnqueueResponse()` 在 response queue 满时丢弃旧响应：`src/runtime/database/data_service/db_thread.cc:273`。
- `DatabaseService::PollResponse()` 是非阻塞轮询：`src/runtime/database/data_service/database_service.cc:289`。

影响：

- 如果上层按 request_id 等待结果，旧响应被丢弃后只能依赖额外超时；若上层没有超时则悬挂。
- 丢响应策略没有按操作类型区分，写失败响应也可能被丢弃，运维只看到 dropped 计数。

建议：

- 为每个 request 建立完成状态或超时通知；队列满时丢弃前必须完成对应 promise/callback。
- 对写操作错误响应使用更高优先级，避免被普通查询淹没。

### P1-9 Admin HTTP 无鉴权，绑定到公网时暴露内部状态

证据：

- Engine 在 `admin_port > 0` 时启动 admin HTTP：`src/runtime/engine/engine.cc:319`。
- Admin server 注册 `/health`、`/stats`、`/metrics`，未见鉴权或来源限制：`src/runtime/monitoring/admin_http.cc:297`。

影响：

- 若 `admin_bind_address` 配成 `0.0.0.0`，攻击者可以读取版本、连接数、DB 状态、物理状态、运行阶段等内部信息。
- 未来如果增加控制类端点，会扩大攻击面。

建议：

- 默认只允许 loopback。
- 增加 bearer token / mTLS / IP allowlist。
- `/metrics` 可独立开关，并支持脱敏。

## P2：中优先级实现问题

### P2-1 Config active environment 不是线程安全状态

证据：

- `SetActiveEnvironment()` 直接写 `active_environment_`：`src/runtime/config/config.h:494`。
- `Reload()` 在可能由 watcher 线程触发时读取 `active_environment_`：`src/runtime/config/config.cc:678`、`src/runtime/config/config.cc:681`。
- `EnableAutoReload()` 的 watcher callback 直接调用 `Reload()`：`src/runtime/config/config.cc:1103`。

影响：

- 如果运行期调用 `SetActiveEnvironment()` 与 auto reload 并发，会有数据竞争。
- profile overlay 的环境选择可能与当前 runtime environment 不一致。

建议：

- 明确规定 active environment 只能启动前设置，并在运行期禁止修改。
- 或将其纳入 `config_mutex_` 保护 / atomic enum。

### P2-2 `ScriptVM::DoString()` 定义了来源白名单但没有使用

证据：

- `IsAllowedDoStringSource()` 定义在 `src/runtime/vm/vm.cc:125`。
- `DoString()` 直接 `luaL_loadbufferx()`，没有调用该检查：`src/runtime/vm/vm.cc:157`。

影响：

- 代码注释声称“Blocks bare user input”，但实际没有阻止。
- DBThread 的 `kExecuteScript` 也会运行请求携带脚本，风险取决于调用入口权限。

建议：

- 要么删除未使用 helper，避免安全错觉；要么在 `DoString()` 中强制检查。
- 对 DB execute script 增加独立开关和权限检查。

### P2-3 Engine cleanup 超时直接 `quick_exit`，会跳过析构和日志 flush

证据：

- cleanup phase 超时后调用 `std::quick_exit(EXIT_FAILURE)`：`src/runtime/engine/engine.cc:565`。

影响：

- 可能跳过 profiler 保存、日志 flush、Mongo cleanup、资源释放。
- 运维只能看到最后一条 critical，无法拿到完整 shutdown 现场。

建议：

- 先进入分阶段强制关闭：停止监听、强断连接、取消 timers、停止 VM。
- 最后再 `abort/quick_exit`，并在此之前强制 flush logger/profiler。

### P2-4 HTTP Service Stop 等待无超时，外部线程调用可能永久阻塞

证据：

- `Service::Stop()` 跨线程时通过 condition_variable 等待 loop 执行 `StopInLoop()`：`src/runtime/evpp/http/service.cc:213`。
- `wait()` 没有 timeout：`src/runtime/evpp/http/service.cc:234`。

影响：

- 如果 event loop 已卡死或不再 dispatch，Stop 调用线程会永久等待。

建议：

- 使用 `wait_for`，超时后返回错误并记录 loop 状态。
- Stop API 返回 bool，调用方决定是否强制释放。

### P2-5 `EventLoop::Stop()` 和若干 Stop API 依赖 assert，不够幂等

证据：

- `EventLoop::Stop()` 断言状态必须是 running：`src/runtime/evpp/event_loop.cc:175`。
- `TCPServer::Stop()` 也断言 running：`src/runtime/evpp/tcp_server.cc:81`。

影响：

- 生产 release 下 assert 可能被去掉，状态错误会走未定义语义。
- debug / 测试环境下重复 stop、半初始化 stop 会直接 abort，不利于容错 cleanup。

建议：

- Stop 系列改成幂等状态机：Stopped/Stopping 直接返回。
- 对非法状态记录 warn，而不是 assert-only。

### P2-6 SpaceManager `CreateSpace()` 在 id 空间耗尽时可能无限循环

证据：

- `CreateSpace()` 使用无限 `for(;;)`，遇到 invalid id 或冲突只 continue：`src/runtime/space/space_manager.cc:16`。

影响：

- 极端情况下 id wrap 后会卡死主线程。
- 虽然 uint64 耗尽概率极低，但服务端基础设施应避免无界循环。

建议：

- 增加尝试上限，失败返回 nullptr 并打 critical。

### P2-7 CMake runtime 模块边界过弱

证据：

- `src/runtime/CMakeLists.txt` 只是一个“大源文件列表”，注释说明无 targets、无编译选项。
- evpp 使用 GLOB 把所有网络子目录一起纳入：`src/runtime/CMakeLists.txt` 中 `file(GLOB EVPP_SOURCES ...)`。

影响：

- 模块间依赖无法由链接器/target 边界约束，任何模块都可以继续直接包含内部头。
- 小改动触发大范围重编译，难以做可选 runtime 裁剪。
- GLOB 让新增文件自动进入构建，容易绕过代码审查约定。

建议：

- 按模块拆 target：runtime_core、runtime_evpp、runtime_script、runtime_db、runtime_physics、runtime_gameplay。
- internal 头通过 target private include 和 facade 公开。
- 禁止关键模块 GLOB，改显式源文件。

### P2-8 注释和文档存在明显编码损坏

证据：

- 多个文件注释出现 `鈥?`、`鈹€` 等乱码，例如 `src/runtime/engine/engine.h`、`src/runtime/CMakeLists.txt`、`src/runtime/database/data_service/*`。

影响：

- 架构注释可读性下降，尤其是生命周期顺序、线程模型说明这类关键文档。
- 容易在 Windows/UTF-8 混用环境下继续扩散。

建议：

- 统一仓库编码为 UTF-8，补 `.editorconfig`。
- 对现有乱码注释集中修复；关键生命周期说明建议保持英文或明确 UTF-8 中文。

## 测试缺口

建议补充以下测试矩阵：

- Engine required subsystem fail-fast：脚本加载失败、DB required 但不可用、physics required 但初始化失败。
- Admin readiness：DB disabled、DB optional、DB required 三种模式。
- Metrics integration：TCP connect/disconnect、send/recv、timer fire、DB request/drop。
- Auto reload lifecycle：Engine 多次 Init/Cleanup 不重复回调；Cleanup 后 reload 不访问旧 Engine。
- ScriptReloader queued callback：Stop 后 event loop 中旧 reload task 不访问已释放对象。
- Space entity timer：Space 内实体 timer 正常触发并在 destroy 后取消。
- Auth negative tests：empty JWT secret、tampered signature、alg none、expired/nbf/issuer/audience。
- DB queue overflow：请求队列满、响应队列满时调用方得到明确失败或超时。

## 建议整改路线

### 第 1 阶段：上线阻断项

1. 修复 metrics 埋点，保证 drain/readiness/stats 有真实数据。
2. 修复 readiness 对 optional DB 的误判。
3. 修复认证：CSPRNG、JWT fail-closed、claim 校验。
4. Engine 初始化增加 required subsystem fail-fast。

### 第 2 阶段：生命周期和线程模型收敛

1. Engine 保存并注销 config reload callback。
2. ScriptReloader 改 weak/generation 安全投递。
3. EntityManager / SpaceManager / Lua callback 全部加线程归属校验。
4. Stop/Cleanup API 幂等化，去掉 assert-only 状态保护。

### 第 3 阶段：模块化和长期维护

1. CMake target 拆分 runtime 子模块。
2. 移除全局 Lua binding 容器，改为 VM-local registry。
3. Space entity 与全局 EntityManager 解耦。
4. 修复编码乱码，补齐架构文档和线程契约。

## 结语

当前 runtime 的功能面较全，但真正上线压力会集中打在“生命周期、线程归属、背压、可观测性、认证”这几条主线上。建议先按 P0/P1 处理，再进行模块 target 拆分。否则后续继续增加玩法系统时，单例和全局绑定会把 bug 从局部实现问题放大成跨模块偶发故障。
