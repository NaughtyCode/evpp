# 数据服务基础设施 (Database Service) 设计方案

> 初稿 — 2026-05-26
> 基于 `src/runtime/database/mongo` + `src/runtime/database/mongo/bind`
> 线程模型参考 `docs/mongo/thread-model-and-design.md`

---

## 1. 顶层规则（不可变）

| # | 规则 | 来源 |
|---|------|------|
| R1 | 主线程只能调用模块的 `Initialize` / `Shutdown` 以及有限的请求发送 API，这些 API 必须线程安全 | 需求 |
| R2 | 一个全局单例类管理整个数据服务基础设施 | 需求 |
| R3 | 基于 `database/mongo` 和 `database/mongo/bind` 实现 | 需求 |
| R4 | 多线程模型：N 个 `DBThread`，线程内阻塞式访问 MongoDB | 需求 + thread-model-and-design.md |
| R5 | 每个 DBThread 内有一个专属的 `DBScriptVM`（`ScriptVM` 子类） | 需求 |
| R6 | `DBScriptVM` 模块外部不可访问 | 需求 |
| R7 | DBThread 默认数量 = 4 | 需求 |
| R8 | 增加数据服务基础设施专用配置文件 | 需求 |
| R9 | 每个 DBThread 创建独立 Quill logger，写入独立日志文件；DBScriptVM 注册日志 API（绑定到该线程的 logger） | 需求 |
| R10 | 调用 `script::ExportMongo(ScriptVM& vm)` 向 DBScriptVM 注册 MongoDB API（mongoc.* / bson.* 模块） | 需求 |
| R11 | 请求处理路径中，CRUD 操作走 `DbOperation` 枚举 + C++ 直接调用；仅 `kExecuteScript` 通过 Lua 脚本执行 DB 操作。`kNoOp` 用于 wakeup 等内部控制 | 新增 |
| R12 | DBScriptVM 加载 `resources/script/runtime` 公共脚本 + 数据服务专属脚本，路径入配置 | 需求 |
| R13 | Redis runtime 编译启用时，DBScriptVM 也导出 `redis` 表，Redis 回调通过本 VM 的 `AsyncResultDispatcher` 回到 DBThread 主循环 dispatch | Redis 模块同步 |

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                         Main Thread (MT)                         │
│                                                                  │
│  DatabaseService::Instance()                                     │
│    ├─ Initialize(config)        // 创建线程、初始化 pool          │
│    ├─ Shutdown()                // join 线程、销毁 pool           │
│    ├─ SendRequest(req)          // SPSC 入队，线程安全            │
│    └─ PollResponse() → resp     // SPSC 出队，线程安全            │
│                                                                  │
│  主线程不允许直接访问 DBThread / DBScriptVM / MongoClient         │
└──────────────────────────┬──────────────────────────────────────┘
                           │
              SPSC queue   │   SPSC queue        SPSC queue
           (request/response per thread, lock-free)
                           │
    ┌──────────────────────┼──────────────────────────┐
    ▼                      ▼                          ▼
┌──────────────┐  ┌──────────────┐          ┌──────────────┐
│  DBThread 0  │  │  DBThread 1  │   ...    │  DBThread 3  │
│              │  │              │          │              │
│ MongoClient* │  │ MongoClient* │          │ MongoClient* │
│ (per-thread  │  │ (per-thread  │          │ (per-thread  │
│  exclusive)  │  │  exclusive)  │          │  exclusive)  │
│              │  │              │          │              │
│ DBScriptVM   │  │ DBScriptVM   │          │ DBScriptVM   │
│ (ScriptVM    │  │ (ScriptVM    │          │ (ScriptVM    │
│  subclass)   │  │  subclass)   │          │  subclass)   │
│              │  │              │          │              │
│ EventLoop()  │  │ EventLoop()  │          │ EventLoop()  │
│ 阻塞 MongoDB  │  │ 阻塞 MongoDB  │          │ 阻塞 MongoDB  │
│ 同步 CRUD     │  │ 同步 CRUD     │          │ 同步 CRUD     │
└──────────────┘  └──────────────┘          └──────────────┘
       │                  │                        │
       └──────────────────┴────────────────────────┘
                          │
                Pop() / Push()  (线程安全, pool 内部 mutex)
                          │
               ┌──────────▼──────────┐
               │  MongoClientPool    │
               │  (共享，线程安全)     │
               │  max_size: N        │
               └─────────────────────┘
```

**关键设计点：**
- 每个 DBThread 在成员变量 `client_` 中持有专属 `MongoClient*`（EventLoop 启动时从 pool Pop 一次，退出时 Push 归还），降低 pool mutex 竞争。与 `thread-model-and-design.md` §3.3 一致。
- 每个 DBThread 在 EventLoop 启动时调用 `CreateLogger()` 创建**独立的 Quill logger**（文件名如 `db_vm_0.log`），与主线程和 Physics 日志完全隔离（R9）。
- 所有 MongoDB 操作在线程内**同步阻塞**执行——这正是 mongo-c-driver 的设计假设。
- MT ↔ DBThread 之间通过 **SPSC lock-free 队列**（moodycamel::ConcurrentQueue）通信，无共享可变状态，无 mutex。队列容量受 `request_queue_size` / `response_queue_size` 限制。
- 所有线程共享一个 `MongoClientPool`，其中 `mongoc_topology_t` 的 SDAM 后台线程由驱动内部管理。

---

## 3. 文件布局

### 3.1 新增文件

```
src/runtime/database/service/
  database_service.h          // DatabaseService 单例（模块对外唯一接口）
  database_service.cc
  db_thread.h                 // DBThread（模块内部）
  db_thread.cc
  db_script_vm.h              // DBScriptVM（模块内部，ScriptVM 子类）
  db_script_vm.cc             // ExportDbLog + ExportDbRuntime 实现
  db_request.h                // DbRequest / DbResponse 数据结构
  db_service_config.h         // DbServiceConfig 配置结构体
  module_access.h             // DATABASE_SERVICE_INTERNAL_ACCESS 宏

resources/config/server/
  db_service.json             // 数据服务专用配置文件（新增）
```

### 3.2 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/runtime/config/config.h` | 增加 `LoadDbServiceConfigFromFile()` 函数声明；`ServerConfig` 增加 `std::string db_service` 路径字段 |
| `src/runtime/config/config_constants.h` | 增加 `kDbServiceConfigFile` 常量 |
| `src/runtime/CMakeLists.txt` | 增加 `DB_SERVICE_SOURCES`，条件编译 `ENGINE_MONGODB_ENABLED` |
| `resources/config/server/server.json` | 增加 `db_service` 字段指向配置文件路径 |

---

## 4. 对外 API 设计（DatabaseService）

DatabaseService 是模块的**唯一对外接口**。主线程只能调用以下 4 个方法：

```cpp
// database_service.h

namespace engine {

class ENGINE_API DatabaseService {
public:
    static DatabaseService& Instance();

    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    // ── 生命周期（MT 独占调用）────────────────────────────────────
    //
    // Initialize: 创建 MongoClientPool + N 个 DBThread、启动所有线程。
    //   uri 来自 ConfigManager::GetMongoDbDevConfig().connection.uri 等。
    //   校验: thread_count < 1 → 返回 false；max_pool_size < thread_count → 警告。
    //   返回 false 表示初始化失败（参数校验失败、pool 创建失败、线程启动失败等）。
    //   必须在 MongoSystem::Instance().Initialize() 之后调用。
    //
    // Shutdown: 优雅停止所有 DBThread（join），排空响应队列，销毁 pool。
    //   必须在 MongoSystem::Instance().Shutdown() 之前调用。
    //   调用后所有未完成的请求/响应将被丢弃。

    bool Initialize(const DbServiceConfig& config, const MongoUri& uri);
    void Shutdown();

    // ── 请求发送（线程安全）────────────────────────────────────────
    //
    // SendRequest: 将请求路由到某个 DBThread 的 SPSC 请求队列。
    //   采用 round-robin 分发，通过 std::atomic 保证线程安全。
    //   返回 false 表示该线程的请求队列已满（背压保护）。
    //
    // PollResponse: 轮询 N 个 DBThread 的响应队列，返回第一个可用的响应。
    //   内部以 round-robin 方式遍历（公平性），单次调用最多检查一圈。
    //   返回 nullptr 表示所有队列均为空（非阻塞）。

    bool SendRequest(DbRequest&& request);
    std::unique_ptr<DbResponse> PollResponse();

    // ── 状态查询（线程安全）────────────────────────────────────────

    bool IsRunning() const;
    bool IsHealthy() const;  // 所有 DBThread 的 healthy_ 均为 true
    int  GetThreadCount() const;
    const DbServiceConfig& GetConfig() const;

private:
    DatabaseService() = default;

    int NextThreadIndex();

    std::atomic<uint64_t> next_thread_{0};
    std::vector<std::unique_ptr<DBThread>> threads_;

    // ── MongoDB 资源 ──────────────────────────────────────────────
    std::unique_ptr<MongoClientPool> pool_;  // owning，Shutdown 时销毁

    DbServiceConfig config_;
    std::atomic<bool> running_{false};

    // PollResponse round-robin 游标（非 atomic——仅 MT 调用，无竞争）
    int poll_cursor_ = 0;
};

} // namespace engine
```

**API 数量：共 4 个核心 API**（+ 2 个状态查询）

### 4.1 PollResponse 轮询策略

```cpp
std::unique_ptr<DbResponse> DatabaseService::PollResponse() {
    int n = static_cast<int>(threads_.size());
    for (int i = 0; i < n; ++i) {
        int idx = (poll_cursor_ + i) % n;
        auto resp = threads_[idx]->DequeueResponse();
        if (resp) {
            poll_cursor_ = (idx + 1) % n;
            return resp;
        }
    }
    return nullptr;  // 所有队列为空
}
```

每个 DBThread 的响应队列是 SPSC（DBT 写入、MT 读取），MT 是唯一消费者，单线程访问，无需锁。

---

## 5. 请求/响应数据结构

```cpp
// db_request.h

namespace engine {

// ── 操作类型 ─────────────────────────────────────────────────────

enum class DbOperation : uint8_t {
    kNoOp = 0,       // 空操作（Stop wakeup sentinel，ProcessRequest 直接跳过）
    kFind,           // 查询（返回游标/文档数组）
    kFindOne,        // 查询单文档
    kInsertOne,      // 插入单文档
    kInsertMany,     // 批量插入
    kUpdateOne,      // 更新单文档
    kUpdateMany,     // 批量更新
    kDeleteOne,      // 删除单文档
    kDeleteMany,     // 批量删除
    kCount,          // 计数
    kAggregate,      // 聚合管道
    kCommand,        // 原生 MongoDB 命令
    kExecuteScript,  // 在 DBScriptVM 上执行 Lua 脚本
};

// ── 请求 ─────────────────────────────────────────────────────────

struct DbRequest {
    uint64_t    request_id = 0;     // 调用方分配，用于匹配响应
    DbOperation operation;
    std::string database;           // 目标数据库
    std::string collection;         // 目标集合（kCommand/kExecuteScript 时可选）
    std::string bson_data;          // 主 BSON/JSON 文档（filter / insert doc / command）
    std::string bson_data2;         // 辅助 BSON/JSON 文档（kUpdate* 的 update 描述、
                                    //   kAggregate 的 pipeline 数组）
    std::string script;             // kExecuteScript 时的 Lua 脚本内容
    int32_t     limit = 0;          // kFind 时限制返回文档数（0 = 不限）
    int32_t     skip = 0;           // kFind 时跳过文档数
};

// ── 响应 ─────────────────────────────────────────────────────────
//
// result_data 序列化格式（JSON）：
//   kNoOp:         — 无响应（EventLoop 层过滤，不生成 DbResponse）
//   kFind:        "[{doc1}, {doc2}, ...]"   — JSON 文档数组
//   kFindOne:     "{doc}"                   — 单个 JSON 文档
//   kInsertOne:   "{\"_id\": \"...\"}"      — 插入文档的 _id
//   kInsertMany:  "{\"inserted_count\": 3}" — 插入数量
//   kUpdateOne:   — 空（见 affected_count）
//   kUpdateMany:  — 空（见 affected_count）
//   kDeleteOne:   — 空（见 affected_count）
//   kDeleteMany:  — 空（见 affected_count）
//   kCount:       "{\"count\": 42}"
//   kAggregate:   "[{doc1}, ...]"           — 聚合结果文档数组
//   kCommand:     "{...}"                   — 任意 JSON 响应
//   kExecuteScript: "..."                   — 脚本输出（可选）

struct DbResponse {
    uint64_t    request_id = 0;     // 匹配请求
    bool        success = false;
    uint32_t    error_code = 0;     // MongoDB error code (matches MongoError::Code())
    std::string error_message;
    std::string result_data;        // BSON/JSON 序列化结果（格式见上表）
    int64_t     affected_count = 0; // insert/update/delete 影响的行数
};

} // namespace engine
```

---

## 6. DBThread 设计（模块内部）

```cpp
// db_thread.h  (DATABASE_SERVICE_INTERNAL_ACCESS 保护)

namespace engine {

class DBThread {
public:
    DBThread(int index, const DbServiceConfig& config);
    ~DBThread();

    DBThread(const DBThread&) = delete;
    DBThread& operator=(const DBThread&) = delete;

    // ── 生命周期（MT 调用）────────────────────────────────────────
    //
    // Start: MT 捕获 config，创建独立 logger + 启动线程。
    //   pool 由 DatabaseService 持有，此处仅保存非 owning 指针。
    //   返回 false 表示 logger 创建或线程启动失败。
    //
    // Stop: 优雅停止——设置 atomic flag → enqueue wakeup → join。
    //   线程退出后 logger 可安全销毁。

    bool Start(MongoClientPool& pool);
    void Stop();

    // ── 请求/响应队列（MT 调用，线程安全 SPSC）────────────────────
    //
    // EnqueueRequest: MT → DBT。返回 false 表示队列满（背压保护）。
    // DequeueResponse: DBT → MT。MT 是唯一消费者，SPSC 无锁安全。

    bool EnqueueRequest(DbRequest&& req);
    std::unique_ptr<DbResponse> DequeueResponse();

    // ── 状态查询 ──────────────────────────────────────────────────

    bool IsRunning() const;
    bool IsHealthy() const;
    int  Index() const { return index_; }
    quill::Logger* GetLogger() const { return logger_; }

    // ── ScriptVM 访问（模块内部专用，compile-time guard 保护）─────

    DBScriptVM& GetScriptVM() { return script_vm_; }

private:
    // ── 日志 ──────────────────────────────────────────────────────
    quill::Logger* CreateDbLogger();

    // ── 线程主循环 + 请求处理 ─────────────────────────────────────
    void EventLoop();
    void ProcessRequest(const DbRequest& req);
    void EnqueueResponse(DbResponse&& resp);  // 含容量检查，见 §6.3

    // ══════════════════════════════════════════════════════════════
    // Members (thread-ownership annotated)
    // ══════════════════════════════════════════════════════════════
    //
    // [MT]   = main-thread-exclusive
    // [DBT]  = db-thread-exclusive (within EventLoop)
    // [MT->] = MT initializes, DBT reads (happens-before via atomic)
    // [SPSC] = SPSC lock-free queue (MT ↔ DBT)
    // [ATOM] = std::atomic

    int index_;                                                    // [MT] thread index
    DbServiceConfig config_;                                       // [MT->] immutable after Start

    // ── 日志（参考 PhysicsThread::CreatePhysicsLogger）───────────
    quill::Logger* logger_ = nullptr;                              // [MT->] created in Start();
                                                                   // [MT+DBT] thread-safe writes
                                                                   // (Quill loggers are inherently MT-safe)

    // ── MongoDB ──────────────────────────────────────────────────
    MongoClientPool* pool_ = nullptr;                              // [MT->] non-owning ref
    MongoClient* client_ = nullptr;                                // [DBT] thread-exclusive

    // ── 脚本 VM ──────────────────────────────────────────────────
    DBScriptVM script_vm_;                                         // [DBT] thread-exclusive

    // ── SPSC 队列（moodycamel::ConcurrentQueue）──────────────────
    moodycamel::ConcurrentQueue<DbRequest>  request_queue_;        // [SPSC] MT → DBT
    moodycamel::ConcurrentQueue<DbResponse> response_queue_;       // [SPSC] DBT → MT

    // ── 线程控制 ─────────────────────────────────────────────────
    std::unique_ptr<std::thread> thread_;                          // [MT] lifecycle
    std::atomic<bool> running_{false};                             // [ATOM] MT write, DBT read
    std::atomic<bool> healthy_{false};                             // [ATOM] DBT write, MT read
};

} // namespace engine
```

### 6.1 DBThread EventLoop 流程

```
EventLoop():

  // ── 1. 获取 MongoClient（阻塞，Pop 超时由 waitQueueTimeoutMS 控制）──
  client_ = pool_->Pop()
  if client_ == nullptr:
    ENGINE_LOG_ERROR(logger_, "DBThread[{}]: pool Pop failed (timeout or pool closed)", index_)
    running_ = false  // 线程即将退出，确保 IsRunning() 报告正确状态
    return  // healthy_ stays false; no client → no cleanup needed

  // ── Init + Main loop ──────────────────────────────────────────
  try {

    // ── 2. 注册 DBScriptVM 的 CustomPtr 槽位 ────────────────────
    script_vm_.RegisterSubsystemObjects(this, client_, pool_)

    // ── 3. 注册 API 绑定 ─────────────────────────────────────────
    ExportDbLog(script_vm_, logger_)    // R9
    script::ExportMongo(script_vm_)     // R10: mongoc.* / bson.* 全局模块表
    script::ExportJson(script_vm_)      // JSON / json_safe
#if defined(ENGINE_REDIS_ENABLED)
    script::ExportRedis(script_vm_)     // R13: redis.command / redis.eval（可选）
#endif
    // ExportMongo 创建全局表变量，注册到 package.loaded 使 require() 可用：
    script_vm_.DoString(
        "package.loaded.mongoc = mongoc; "
        "package.loaded.bson   = bson")
    ExportDbRuntime(script_vm_)         // db_get_client / db_get_pool 全局函数
    script::ExportTimer(script_vm_, *timer_mgr_)
    ExportImport(script_vm_)

    // ── 4. 设置脚本 import 路径（R12） ──────────────────────────
    script_vm_.SetImportPath(
        config_.script.db_scripts_dir + ";" +
        config_.script.runtime_scripts_dir)

    // ── 5. Lua 初始化（R12: 先公共后专属）───────────────────────
    if config_.script.auto_load:
      script_vm_.DoDirectory(config_.script.runtime_scripts_dir)
      script_vm_.DoDirectory(config_.script.db_scripts_dir)
    script_vm_.InitScript()

    healthy_ = true

    // ── 6. 主循环 ────────────────────────────────────────────────
    while running_:
      try {
        if req = request_queue_.try_dequeue():
          if req.operation != DbOperation::kNoOp:
            ProcessRequest(req)
        else:
            script_vm_.DispatchAsyncResults(256)
            script_vm_.CallFrameCallback(frame_count, delta)
            timer_mgr_->update()
          // 按 target_fps 控制 DBThread frame rate，空闲时短暂 sleep
      } catch (const std::exception& e) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: exception in event loop: {}",
                        index_, e.what())
        healthy_ = false
        running_ = false
        break  // → 跳到 finally 块执行清理
      } catch (...) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: unknown exception in event loop",
                        index_)
        healthy_ = false
        running_ = false
        break
      }

  } catch (const std::exception& e) {
    ENGINE_LOG_ERROR(logger_, "DBThread[{}]: init phase exception: {}",
                    index_, e.what())
  } catch (...) {
    ENGINE_LOG_ERROR(logger_, "DBThread[{}]: init phase unknown exception",
                    index_)
  }

  // ── 7. finally: 始终执行的清理（正常退出/init异常/loop异常 均到达）──
  healthy_ = false
  script_vm_.DestroyScript()
  pool_->Push(client_)   // Pop 成功后必须归还，否则 pool 资源泄漏
  running_ = false
```

### 6.2 Start / Stop 实现要点

**Start()**（MT 调用）：
1. 创建独立 logger：`logger_ = CreateDbLogger()`，失败返回 false
2. 保存 pool 引用：`pool_ = &pool`
3. `running_ = true`，启动线程：`thread_ = std::make_unique<std::thread>(&DBThread::EventLoop, this)`

**Stop()**（MT 调用）：
1. 检查 `running_`，未运行则直接返回
2. `running_ = false`
3. **Wakeup 机制**：向 `request_queue_` 入队一条 `DbOperation::kNoOp` 请求，确保 EventLoop 从 sleep 中醒来。注意：若线程尚未进入主循环（仍阻塞在 `pool_->Pop()`），wakeup 无效——此时依赖 `waitQueueTimeoutMS` 超时使 Pop 返回 nullptr，线程随后自然退出。这也意味着 `waitQueueTimeoutMS` 决定了 Shutdown 的最坏响应延迟。
4. `thread_->join()` + `thread_.reset()`

### 6.3 队列容量控制

参照 PhysicsThread 的帧堆积保护 [D23]，通过 `size_approx()` 做容量检查：

```cpp
bool DBThread::EnqueueRequest(DbRequest&& req) {
    if (!running_.load(std::memory_order_acquire)) return false;

    size_t sz = request_queue_.size_approx();
    if (static_cast<int>(sz) >= config_.thread_pool.request_queue_size) {
        ENGINE_LOG_WARN(logger_, "DBThread[{}]: request queue full (approx={}, max={})",
                        index_, sz, config_.thread_pool.request_queue_size);
        return false;
    }
    return request_queue_.enqueue(std::move(req));
}
```

响应队列同样在 EventLoop 入队端检查容量（丢弃最旧的响应）：

```cpp
// 入队响应前：若队列满则丢弃最旧
while (response_queue_.size_approx() >=
       static_cast<size_t>(config_.thread_pool.response_queue_size)) {
    DbResponse dropped;
    response_queue_.try_dequeue(dropped);
    ENGINE_LOG_WARN(logger_, "DBThread[{}]: response queue full, dropped response [id={}]",
                    index_, dropped.request_id);
}
response_queue_.enqueue(std::move(response));
```

### 6.4 ProcessRequest 实现

```cpp
void DBThread::ProcessRequest(const DbRequest& req) {
    DbResponse resp;
    resp.request_id = req.request_id;

    if (req.operation == DbOperation::kNoOp) return;

    // kExecuteScript 不需要 database/collection —— 脚本内自行操作
    if (req.operation == DbOperation::kExecuteScript) {
        try {
            resp.success = script_vm_.DoString(req.script, "db_request",
                                               &resp.error_message);
            // 脚本通过 ExportDbLog / ExportMongo API 直接操作 DB
            // DoString 返回 true/false；若 Lua 显式 return 字符串，
            // 可通过 lua_tostring(L, -1) 读取到 resp.result_data
        } catch (const std::exception& e) {
            resp.success = false;
            resp.error_message = e.what();
        }
        EnqueueResponse(std::move(resp));
        return;
    }

    // CRUD 路径：按需获取 database / collection 句柄
    // kCommand 只需要 db（通过 client_->CommandSimple / db->CommandSimple）
    // 其他 CRUD 操作需要 coll
    engine::mongo::MongoDatabase*   db   = nullptr;
    engine::mongo::MongoCollection* coll = nullptr;

    try {
        bool need_db   = (req.operation == DbOperation::kCommand);
        bool need_coll = (req.operation != DbOperation::kCommand);

        if (need_db) {
            db = client_->GetDatabase(req.database);
        }
        if (need_coll && !req.collection.empty()) {
            coll = client_->GetCollection(req.database, req.collection);
        }

        switch (req.operation) {
        case DbOperation::kFind: {
            auto filter = BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto* cursor = coll->FindWithOpts(filter, nullptr, nullptr);
            if (req.limit > 0) cursor->SetLimit(req.limit);
            resp.result_data = SerializeCursor(cursor, req.skip);
            cursor->Destroy();
            resp.success = true;
            break;
        }
        case DbOperation::kFindOne: {
            auto filter = BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto* cursor = coll->FindWithOpts(filter, nullptr, nullptr);
            cursor->SetLimit(1);
            BsonDocument doc;
            if (cursor->Next(&doc)) resp.result_data = doc.ToJson();
            cursor->Destroy();
            resp.success = true;
            break;
        }
        case DbOperation::kInsertOne: {
            auto doc = BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            BsonDocument reply;
            MongoError err;
            // InsertOne(doc, opts, reply, error)
            resp.success = coll->InsertOne(doc, nullptr, &reply, &err);
            if (resp.success) resp.result_data = reply.ToJson();
            else { resp.error_code = err.Code(); resp.error_message = err.Message(); }
            break;
        }
        case DbOperation::kUpdateOne: {
            auto filter = BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto update = BsonDocument::NewFromJson(
                req.bson_data2.c_str(), req.bson_data2.size());
            BsonDocument reply;
            MongoError err;
            // UpdateOne(selector, update, opts, reply, error)
            resp.success = coll->UpdateOne(filter, update, nullptr, &reply, &err);
            if (resp.success) {
                // 从 reply 中提取 nModified（使用 BsonIter）
                BsonIter iter;
                if (iter.InitFind(reply, "nModified")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kUpdateMany: {
            auto filter = BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto update = BsonDocument::NewFromJson(
                req.bson_data2.c_str(), req.bson_data2.size());
            BsonDocument reply;
            MongoError err;
            // UpdateMany(selector, update, opts, reply, error)
            resp.success = coll->UpdateMany(filter, update, nullptr, &reply, &err);
            if (resp.success) {
                BsonIter iter;
                if (iter.InitFind(reply, "nModified")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        // kInsertMany / kDeleteOne / kDeleteMany / kCount / kAggregate / kCommand:
        //   类似模式（kInsertMany 需从 bson_data 解析 JSON 数组，构建 BsonDocument* 数组传入 InsertMany）
        default:
            resp.success = false;
            resp.error_message = "unsupported operation";
            break;
        }
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
    }

    // RAII 清理（nullptr 检查，即使异常也会在 catch 后执行）
    if (coll) coll->Destroy();
    if (db)   db->Destroy();

    EnqueueResponse(std::move(resp));
}
```

要点：
- 所有 CRUD 在 DBT 内**同步阻塞**执行（mongo-c-driver 设计假设）。
- `kNoOp` 由 EventLoop 层过滤，ProcessRequest 内也有防御性 early-return。
- `kExecuteScript` **不**创建 database/collection 句柄——脚本通过已注册的 ExportMongo API 自行操作 DB。
- `kUpdateOne` / `kUpdateMany`：filter 从 `bson_data` 解析，update 描述从 `bson_data2` 解析。
- `kCommand` 仅需要 `db` 句柄（或通过 `client_->CommandSimple`），`kAggregate` 可能在 db 或 collection 层级。
- `db`/`coll` 指针：初始化为 nullptr，按需赋值，catch 后通过 nullptr 检查安全清理。
- 输入校验（实现阶段）：所有 CRUD 操作均应对 `bson_data` / `database` / `collection` 做非空校验。写操作尤其是 `DeleteMany` 若传入空 filter 会清空整个集合；读操作若 collection 为空则 `GetCollection` 可能返回无效句柄导致 nullptr 解引用。
- `affected_count` 通过 BsonIter 从 reply 文档提取（`InitFind("nModified")` → `AsInt64()`）。
- `SerializeCursor` 是内部辅助函数，遍历 cursor 并将文档序列化为 JSON 数组。
- `kInsertMany`：`bson_data` 为 JSON 数组，需逐元素解析为 `BsonDocument`，构建 `BsonDocument*[]` 数组后传入 `coll->InsertMany()`。
- `kDeleteOne` / `kDeleteMany`：selector 从 `bson_data` 解析，调用 `coll->DeleteOne()` / `coll->DeleteMany()`。
- `kCount`：调用 `coll->CountDocuments()`，结果写入 `resp.result_data`。
- `kAggregate`：pipeline 从 `bson_data` 解析为 JSON 数组（或使用 `bson_data2`），调用 `coll->Aggregate()`，序列化 cursor。
- `kCommand`：仅需 `db` 句柄，通过 `client_->CommandSimple()` 或 `db->CommandSimple()` 执行。

---

## 7. DBScriptVM 设计（模块内部，外部不可见）

参照 `PhysicsScriptVM` 的模式。

```cpp
// db_script_vm.h  (DATABASE_SERVICE_INTERNAL_ACCESS 保护)

namespace engine {

// ── CustomPtr 槽位枚举 ───────────────────────────────────────────

enum DbCustomPtr : int {
    kDbPtrDBThread  = 1,  // DBThread*
    kDbPtrScriptVM  = 2,  // DBScriptVM*
    kDbPtrClient    = 3,  // MongoClient*
    kDbPtrPool      = 4,  // MongoClientPool*
};

// ── DBScriptVM ────────────────────────────────────────────────────

class DBScriptVM : public ScriptVM {
public:
    DBScriptVM();
    ~DBScriptVM() override;

    DBScriptVM(const DBScriptVM&) = delete;
    DBScriptVM& operator=(const DBScriptVM&) = delete;

    // 注册 DB 子系统对象到 CustomPtrStore（DBT EventLoop 启动时调用）
    // 内部先 ReserveCustomPtrSlots(4) 预分配槽位，再按 DbCustomPtr 索引写入。
    void RegisterSubsystemObjects(DBThread* thread,
                                  MongoClient* client,
                                  MongoClientPool* pool);

    // 类型化访问器（DBT 独占调用，线程安全由 DBT 独占保证）
    DBThread*        GetDBThread() const;
    MongoClient*     GetMongoClient() const;
    MongoClientPool* GetMongoClientPool() const;

    bool AreCoreSlotsValid() const;

private:
};

} // namespace engine
```

### 7.1 DBScriptVM 关键设计点

- **继承 ScriptVM**：复用 Lua state 管理、脚本加载、模块导入（`ScriptImporter`）等基础设施。
- **注册日志 API（R9）**：不使用全局 `script::ExportLog`（它绑定到 "root" logger），而是在 `db_script_vm.cc` 中实现 `ExportDbLog(ScriptVM& vm, quill::Logger* logger)`，将 `log_trace` / `log_debug` / `log_info` / `log_warn` / `log_error` / `log_fatal` 绑定到 **该 DBThread 的专属 logger**。Lua 脚本中调用这些函数时，日志输出到 `logs/db_service/db_vm_{N}.log`。
- **注册 MongoDB API（R10）**：调用 `script::ExportMongo(script_vm_)` 创建 `mongoc` 和 `bson` 全局模块表；随后将全局表注册到 `package.loaded`（`package.loaded.mongoc = mongoc`），使 Lua 脚本既可通过全局变量也可通过 `require("mongoc")` 访问。
- **注册 Redis API（R13，可选）**：当目标编译了 `ENGINE_REDIS_ENABLED` 时，调用 `script::ExportRedis(script_vm_)` 创建 `redis` 全局表并注册 `package.loaded.redis`。DBThread 中的 Redis Lua callback 不在 Redis worker 线程执行，而是在本 DBThread 循环调用 `script_vm_.DispatchAsyncResults()` 时执行。
- **CustomPtr 注册**：将 DBThread、MongoClient、MongoClientPool 指针注册到 VM，Lua 侧通过 custom-ptr 索引访问 C++ 对象。在 EventLoop 中于 `ExportMongo` / `InitScript` 之前调用。
- **脚本加载（R12）**：先加载 `runtime_scripts_dir`（`resources/script/runtime`）公共运行时脚本，再加载 `db_scripts_dir`（`resources/script/db_service`）数据服务专属脚本。后加载的脚本可覆盖前者的全局定义。`require` 搜索路径按 `db_scripts_dir;runtime_scripts_dir` 顺序。
- **访问控制**：头文件受 `DATABASE_SERVICE_INTERNAL_ACCESS` 宏保护，外部编译报错。

### 7.2 ExportDbLog 实现

```cpp
// db_script_vm.cc 内部函数，模块外部不可见

namespace {

// 每个 log 函数捕获 DBThread 专属 logger（通过 upvalue / lightuserdata）
int l_db_log_info(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = static_cast<quill::Logger*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    ENGINE_LOG_INFO(logger, "[lua] {}", msg);
    return 0;
}
// ... l_db_log_trace, l_db_log_debug, l_db_log_warn, l_db_log_error, l_db_log_fatal

} // namespace

void ExportDbLog(ScriptVM& vm, quill::Logger* logger) {
    auto L = vm.GetState();

    // 将 logger 作为 lightuserdata 压栈一次，每个 closure 通过 lua_pushvalue
    // 复制它作为 upvalue。最后一个 pushcclosure 直接消费原始值。
    lua_pushlightuserdata(L, logger);

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_trace, 1); lua_setglobal(L, "log_trace");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_debug, 1); lua_setglobal(L, "log_debug");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_info,  1); lua_setglobal(L, "log_info");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_warn,  1); lua_setglobal(L, "log_warn");
    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_log_error, 1); lua_setglobal(L, "log_error");
    lua_pushcclosure(L, l_db_log_fatal, 1);                     lua_setglobal(L, "log_fatal");
    // 最后一个 closure 消费了原始 logger 值，栈已清空，无需 pop
}
```

### 7.3 Lua 视角

在 DBScriptVM 中运行的 Lua 脚本可以：

```lua
-- R9: 日志输出到本 DBThread 的专属日志文件
log_info("player login: uid=" .. uid)
log_error("query failed: " .. err)

-- R10: 使用 mongoc / bson 模块操作 MongoDB
local bson = require("bson")
local mongoc = require("mongoc")

local doc = bson.doc.new()
doc:append("name", "player_001")
doc:append("score", 100)

-- 通过 custom-ptr 获取 MongoClient（C++ 侧注入）
local client_ptr = db_get_client()
local coll = mongoc.collection.new(client_ptr, "game_db", "players")
coll:insert_one(doc)

-- R13: Redis runtime 编译并启用后，可异步访问 Redis
redis.command({"GET", "player:001:cache"}, function(result)
    if result.ok and result.value.type == "string" then
        log_info("cache=" .. result.value.value)
    else
        log_warn("redis failed: " .. tostring(result.error))
    end
end, { routing_key = "player:001" })
```

### 7.4 ExportDbRuntime — 注册 DBScriptVM 专用运行时绑定

`ExportMongo` 注册了通用的 `mongoc.*` / `bson.*` 模块，但 Lua 脚本还需要获取当前线程的 `MongoClient*`（从 pool 借出的）、`MongoClientPool*` 等对象。这些通过 CustomPtr 槽位提供，需要在 Lua 侧暴露为 `db.*` 全局函数。

```cpp
// db_script_vm.cc 内部函数

namespace {

// 从当前 VM 的 CustomPtr 槽位读取 MongoClient*，作为 lightuserdata 返回
int l_db_get_client(lua_State* L) {
    auto& vm = *static_cast<DBScriptVM*>(
        lua_touserdata(L, lua_upvalueindex(1)));  // DBScriptVM* 作为 upvalue
    auto* client = vm.GetMongoClient();
    if (!client) {
        lua_pushnil(L);
        lua_pushstring(L, "MongoClient not available");
        return 2;
    }
    lua_pushlightuserdata(L, static_cast<void*>(client));
    return 1;
}

int l_db_get_pool(lua_State* L) {
    auto& vm = *static_cast<DBScriptVM*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    auto* pool = vm.GetMongoClientPool();
    lua_pushlightuserdata(L, static_cast<void*>(pool));
    return 1;
}

} // namespace

void ExportDbRuntime(ScriptVM& vm) {
    auto L = vm.GetState();
    // 将 DBScriptVM* 作为 upvalue，让 Lua 函数可以访问 CustomPtr 槽位
    lua_pushlightuserdata(L, &vm);

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_get_client, 1);
    lua_setglobal(L, "db_get_client");

    lua_pushvalue(L, -1); lua_pushcclosure(L, l_db_get_pool, 1);
    lua_setglobal(L, "db_get_pool");

    lua_pop(L, 1);  // 弹出原始 vm 指针
}
```

在 EventLoop 初始化序列中，`ExportDbRuntime` 在 `ExportMongo` 之后、`InitScript` 之前调用，使 Lua 脚本可以通过 `db_get_client()` 获取 `MongoClient*` 指针，传递给 `mongoc.collection.new()` 等 API。`ExportRedis` 与 `ExportJson` 同样在 `InitScript` 前完成，确保 DB 服务脚本初始化阶段即可提交 Redis 请求；完成回调要等 DBThread 后续 frame dispatch。

---

## 8. 访问控制机制

参照 `PHYSICS_INTERNAL_ACCESS` 模式：

```cpp
// module_access.h

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error "db_thread.h / db_script_vm.h are internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including these headers."
#endif
```

`db_thread.h` 和 `db_script_vm.h` 顶部包含此检查，确保外部代码只能通过 `database_service.h` 访问模块。

---

## 9. 配置设计

### 9.1 配置结构体

```cpp
// db_service_config.h

namespace engine {

struct DbLogConfig {
    std::string dir = "logs/db_service";   // 独立日志目录（与主日志隔离）
    std::string level = "info";            // trace/debug/info/warn/error/fatal
    int rotation_size_mb = 100;            // 日志文件轮转大小
    int max_backup_files = 10;             // 最大保留备份文件数
};

struct DbScriptConfig {
    std::string runtime_scripts_dir = "resources/script/runtime";   // R12: 公共运行时脚本
    std::string db_scripts_dir = "resources/script/db_service";     // R12: 数据服务专属脚本
    bool        auto_load = true;      // 启动时自动加载脚本目录
};

struct DbThreadPoolConfig {
    int thread_count = 4;              // DBThread 数量（默认 4）
    int request_queue_size = 1024;     // 每个线程的请求队列容量
    int response_queue_size = 1024;    // 每个线程的响应队列容量
};

struct DbConnectionPoolConfig {
    int max_pool_size = 16;            // MongoClientPool 最大 client 数
                                       //   必须 >= thread_count，建议 thread_count * 2
                                       //   过小会导致 Pop 阻塞超时
    int wait_queue_timeout_ms = 5000;  // Pop 阻塞超时（毫秒），0 = 永不超时（危险：
                                       //   若设为 0 且 pool 中无空闲 client，Pop 将永久阻塞，
                                       //   导致 Stop() join 挂起。强烈建议设置正整数值）
                                       //   注意：此值通过 MongoUri::SetOptionAsInt32 注入：
                                       //   uri.SetOptionAsInt32("waitQueueTimeoutMS", timeout_ms)
};

struct DbServiceConfig {
    DbLogConfig log;                       // R9: 独立日志配置
    DbThreadPoolConfig thread_pool;
    DbConnectionPoolConfig connection_pool;
    DbScriptConfig script;
};

// ── CreateDbLogger 内部映射（DBThread::CreateDbLogger）─────────────
// 将 DbLogConfig 映射为 engine::LogConfig 后调用 engine::CreateLogger():
//   LogConfig.logger_name    = "db_vm_" + std::to_string(index_)
//   LogConfig.log_filename   = ""   （logger_name 作为文件名前缀）
//   LogConfig.dir            = DbLogConfig::dir
//   LogConfig.level          = DbLogConfig::level
//   LogConfig.rotation_size_mb   = DbLogConfig::rotation_size_mb
//   LogConfig.max_backup_files   = DbLogConfig::max_backup_files
//   LogConfig.rotation_frequency  = ""      （仅按大小轮转）
//   LogConfig.rotation_interval   = 1
//   LogConfig.rotation_time_daily = "00:00"
//   LogConfig.format_pattern      = 默认值（Quill 内置格式）
// 最终日志文件: logs/db_service/db_vm_0_20260526_210000.log

} // namespace engine
```

### 9.2 配置文件（JSON）

```json
// resources/config/server/db_service.json

{
  "log": {
    "dir": "logs/db_service",
    "level": "info",
    "rotation_size_mb": 100,
    "max_backup_files": 10
  },
  "thread_pool": {
    "thread_count": 4,
    "request_queue_size": 1024,
    "response_queue_size": 1024
  },
  "connection_pool": {
    "max_pool_size": 16,
    "wait_queue_timeout_ms": 5000
  },
  "script": {
    "runtime_scripts_dir": "resources/script/runtime",
    "db_scripts_dir": "resources/script/db_service",
    "auto_load": true
  }
}
```

### 9.3 配置加载路径

```
server.json
  └─ "db_service": "resources/config/server/db_service.json"  ← 新增字段
```

在 `ConfigManager::LoadServerFromFile()` 加载 server.json 后，解析 `db_service` 路径并调用 `LoadDbServiceConfigFromFile()`。

---

## 10. 生命周期

```
┌──────────────────────────────────────────────────────────────┐
│  1. MongoSystem::Instance().Initialize()                     │
│     (mongoc_init，全局一次)                                   │
├──────────────────────────────────────────────────────────────┤
│  2. DatabaseService::Instance().Initialize(config, uri)      │
│     │                                                        │
│     │  uri 来源: ConfigManager::GetMongoDbDevConfig()         │
│     │           .connection.uri                              │
│     │                                                        │
│     ├─ pool_ = MongoClientPool::New(uri)                     │
│     ├─ pool_->SetMaxSize(config.connection_pool.max_pool_size)│
│     ├─ 创建 N 个 DBThread                                     │
│     └─ 逐个 DBThread::Start(*pool_) → 启动线程                │
│         └─ 每个线程: | logger CreateLogger | Pop client      │
│                      | Export* bindings | InitScript | Loop  │
├──────────────────────────────────────────────────────────────┤
│  3. 运行期                                                    │
│     ┌────────────────────────────────────────────┐           │
│     │ MT:                                         │           │
│     │   SendRequest(req) → SPSC →                │           │
│     │   PollResponse()  ← SPSC ←                 │           │
│     │                                             │           │
│     │ DBThread (×N):                              │           │
│     │   EventLoop → ProcessRequest → 阻塞 MongoDB  │           │
│     │             → response_queue_.enqueue()     │           │
│     └────────────────────────────────────────────┘           │
├──────────────────────────────────────────────────────────────┤
│  4. DatabaseService::Instance().Shutdown()                   │
│     ├─ 逐个 DBThread::Stop()                                  │
│     │   ├─ running_ = false（atomic store, release）          │
│     │   ├─ 入队 wakeup sentinel（确保线程从 sleep/Pop 中醒来）  │
│     │   ├─ thread_->join()                                   │
│     │   └─ 线程内: Push client → DestroyScript → logger 可复用│
│     ├─ DrainResponses(): 排空所有响应队列（已完成的响应被丢弃）   │
│     ├─ pool_->Destroy()   // 非线程安全，必须在 join 后调用      │
│     └─ pool_.reset()     → 释放 C++ wrapper 内存              │
├──────────────────────────────────────────────────────────────┤
│  5. MongoSystem::Instance().Shutdown()                       │
│     (mongoc_cleanup，全局一次)                                │
└──────────────────────────────────────────────────────────────┘

注: Shutdown 后所有未完成的请求和已入队但未消费的响应都将被丢弃。
     这是预期行为——模块不保证 at-most-once 之外的消息语义。

---

## 11. 请求路由策略

采用 **round-robin** 分发，保证负载均匀：

```cpp
int DatabaseService::NextThreadIndex() {
    return next_thread_.fetch_add(1, std::memory_order_relaxed) % threads_.size();
}

bool DatabaseService::SendRequest(DbRequest&& request) {
    int idx = NextThreadIndex();
    return threads_[idx]->EnqueueRequest(std::move(request));
}
```

后续可扩展为 hash-based 路由（按 database/collection 亲和性），但 round-robin 作为初版足够。

---

## 12. 实现步骤

| 阶段 | 文件 | 内容 |
|------|------|------|
| 1 | `db_request.h` | DbOperation、DbRequest、DbResponse 数据结构 |
| 2 | `db_service_config.h` | DbServiceConfig 及子结构体（含 DbLogConfig） |
| 3 | `module_access.h` | DATABASE_SERVICE_INTERNAL_ACCESS 宏 |
| 4 | `db_script_vm.h/.cc` | DBScriptVM（ScriptVM 子类，CustomPtr 注册，ExportDbLog + ExportDbRuntime + ExportMongo/ExportRedis 调用） |
| 5 | `db_thread.h/.cc` | DBThread（CreateDbLogger，Start/Stop/wakeup，EventLoop，SPSC 队列及容量控制，ProcessRequest 完整实现） |
| 6 | `database_service.h/.cc` | DatabaseService 单例（pool 创建/销毁，Initialize/Shutdown，SendRequest round-robin，PollResponse 跨队列轮询） |
| 7 | `resources/config/server/db_service.json` | 专用配置文件 |
| 8 | `config.h/.cc` + `config_constants.h` | 增加 `LoadDbServiceConfigFromFile()` 函数；`ServerConfig` 增加 `std::string db_service` 路径字段；增加 `kDbServiceConfigFile` 常量 |
| 9 | `server.json` | 增加 `db_service` 字段 |
| 10 | `CMakeLists.txt` | DB_SERVICE_SOURCES + source_group + ENGINE_MONGODB_ENABLED 条件编译 |

---

## 13. 与现有模块的关系

```
Engine::Init()
  ├─ ConfigManager::Load()          // 解析所有 JSON 配置（含 server.json → db_service.json）
  ├─ MongoSystem::Instance().Initialize()
  │
  ├─ // 构造 URI（从已加载的 MongoDB 集群配置，通过 SetOption 注入 pool 超时参数）
  │  auto& mongo_cfg = ConfigManager::Instance().GetMongoDbDevConfig();
  │  auto uri = MongoUri::New(mongo_cfg.connection.uri.c_str());
  │  if (db_svc_config.connection_pool.wait_queue_timeout_ms > 0) {
  │      uri.SetOptionAsInt32("waitQueueTimeoutMS",
  │          static_cast<int32_t>(db_svc_config.connection_pool.wait_queue_timeout_ms));
  │  }
  │
  ├─ DatabaseService::Instance().Initialize(db_svc_config, uri)
  └─ ...

Engine::Cleanup()
  ├─ DatabaseService::Instance().Shutdown()
  ├─ MongoSystem::Instance().Shutdown()
  └─ ...
```

DatabaseService 位于 `MongoSystem` 生命周期之内：Initialize 在 `mongoc_init` 之后，Shutdown 在 `mongoc_cleanup` 之前。`MongoUri` 从 `ConfigManager::GetMongoDbDevConfig().connection.uri` 构造。
