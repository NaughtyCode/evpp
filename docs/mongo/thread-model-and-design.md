# mongo-c-driver 线程模型分析与多线程阻塞 Client 方案设计

> 日期: 2026-05-26
> 基于: mongo-c-driver (src/thirdparty/mongo-c-driver) + 现有 engine::mongo C++ 封装层

---

## 1. mongo-c-driver 线程模型分析

### 1.1 核心原则

mongo-c-driver 的线程安全设计遵循一个简单规则:

```
mongoc_client_t        → 非线程安全 (单线程独占)
mongoc_client_pool_t   → 线程安全 (除 destroy)
```

**`mongoc_client_t` 不是线程安全的**, 不可多线程共享。其内部 `mongoc_cluster_t` 维护到各 MongoDB server 的 socket 连接集合（`mongoc_set_t *nodes`，每个已知 server 一个 `mongoc_cluster_node_t`，含 `mongoc_stream_t *stream`），所有读写操作直接访问这些 stream 而**没有任何互斥锁保护**（`mongoc_cluster_t` 结构体中无 mutex/condvar/atomic 成员）。因此每个线程必须独占一个 `mongoc_client_t`。

### 1.2 线程原语层

mongo-c-driver 在 `src/common/src/common-thread-private.h` 提供了跨平台线程抽象:

| 原语 | POSIX | Windows | 用途 |
|------|-------|---------|------|
| `bson_mutex_t` | `pthread_mutex_t` | `CRITICAL_SECTION` | 互斥锁 |
| `bson_shared_mutex_t` | `pthread_rwlock_t` | `SRWLOCK` | 读写锁 |
| `bson_thread_t` | `pthread_t` | `HANDLE` | 线程句柄 |
| `bson_once_t` | `pthread_once_t` | `INIT_ONCE` | 一次性初始化 |
| `mongoc_cond_t` | `pthread_cond_t` | `CONDITION_VARIABLE` | 条件变量 |

**注意**: `bson_shared_mutex_t` 的实际用途：
- `mongoc-shared.c`: 全局 `g_shared_ptr_mtx` 保护 `mc_shared_tpld` 的原子 load/store（读持共享锁，写持独占锁）
- `mongoc-scram.c` / `mongoc-oidc-cache.c`: 内部状态保护

**Topology 自身不使用读写锁**——其内部状态保护使用两个独立的 `bson_mutex_t`（`srv_polling_mtx` + `tpld_modification_mtx`），外加两个 `mongoc_cond_t`（`cond_client` + `srv_polling_cond`）和两个原子变量（`scanner_state`、`_atomic_srv_polling_rescan_interval_ms`）。Topology description 的跨线程共享通过 `mc_shared_tpld` 引用计数指针实现，其 load/store 操作使用全局 `bson_shared_mutex_t`（见 1.8 节）。

线程创建使用 `mcommon_thread_create()` / `mcommon_thread_join()`。

### 1.3 mongoc_client_pool_t 内部结构

```
src/libmongoc/src/mongoc/mongoc-client-pool.c
```

```c
struct _mongoc_client_pool_t {
    bson_mutex_t    mutex;          // 保护所有 pool 状态 (pop/push/set_*)
    mongoc_cond_t   cond;           // 当 client 归还时唤醒等待者
    mongoc_queue_t  queue;          // LIFO 空闲 client 队列 (单链表: head/tail/length)
    mongoc_topology_t *topology;    // 所有 client 共享的拓扑(集群状态)
    mongoc_uri_t    *uri;           // 连接 URI
    uint32_t        max_pool_size;  // 默认 100
    uint32_t        size;           // 当前已创建的 client 数量
#ifdef MONGOC_ENABLE_SSL
    mongoc_ssl_opt_t ssl_opts;      // SSL 配置 (仅当 SSL 编译启用)
    bool            ssl_opts_set;
#endif
    bool            apm_callbacks_set;       // 是否设置了 APM 回调
    bool            error_api_set;           // 是否设置了错误 API 版本
    bool            structured_log_opts_set; // 是否设置了结构化日志
    bool            client_initialized;      // 首次 pop 后锁定配置 (阻止 set_server_api 等)
    int32_t         error_api_version;       // 错误 API 版本号
    mongoc_server_api_t *api;               // 服务端 API 版本约束
    mongoc_array_t  last_known_serverids;   // 已排序的 uint32_t 数组, push 时用于清理过期连接
};
```

**关键点**:

1. 所有 pool 中的 client 共享同一个 `mongoc_topology_t`，其中包含多个共享资源：
   - SDAM (Server Discovery and Monitoring) 状态和后台监控线程
   - `mongoc_server_session_pool`（线程安全的 LIFO session 池，`bson_mutex_t` 保护）
   - `mongoc_log_and_monitor_instance_t`（APM 回调、结构化日志）
   - `mongoc_oidc_cache_t`（OIDC 认证缓存）
   - 客户端加密状态（`mongoc_topology_cse_state_t`，可选）
2. `client_initialized` 标志位在首次 `pop()` 成功创建新 client 后（`_initialize_new_client()` 内）设置为 true。之后以下 API 被阻止调用（返回 false 或 assert）：
   - `set_server_api` — 返回 false + error
   - `set_structured_log_opts` — 返回 false
   - `set_oidc_callback` — 返回 false
   - `set_error_api` — 由独立的 `error_api_set` 标志位阻止（同样只在首次 pop 前有效）
   - `set_apm_callbacks` — 特殊：允许调用但输出警告（历史遗留，向后兼容）
3. `last_known_serverids` 是一个已排序的 `uint32_t` 数组，在 `push()` 归还 client 时，pool 将其与当前 topology 中已知的 server id 集合对比，清理不属于当前集群拓扑的连接。
4. **`min_pool_size` 不存在**——该参数已被 mongo-c-driver 弃用并移除 (CDRIVER-2390)。Pool 始终从空队列开始，按需创建 client。

### 1.4 Pool 操作流程

**`mongoc_client_pool_pop()` (阻塞获取)**:
```
1. lock(pool->mutex)
2. if queue 非空 → pop LIFO 队列 (pop_head) → 返回 client
3. if size < max_pool_size → 创建新 client (_mongoc_client_new_from_topology) →
   _initialize_new_client() 逐项复制 pool 配置到 client:
   - error_api_version (int32_t 直接赋值)
   - server_api (mongoc_server_api_copy 深拷贝)
   - SSL opts (条件编译, 仅在 MONGOC_ENABLE_SSL 且 ssl_opts_set 时)
   - stream_initiator (测试用)
   (注: APM callbacks 和 appname 存储在 topology 层级, 所有 client 自动共享, 无需复制)
   → size++ → client_initialized = true → 返回
4. otherwise → cond_wait(&pool->cond, &pool->mutex)  // 阻塞等待
   - 若设置了 waitQueueTimeoutMS > 0 → cond_timedwait (计算剩余时间) → 超时返回 NULL
   - waitQueueTimeoutMS 默认值: -1 (无限等待), 设为 0 也等同于无限等待
5. unlock(pool->mutex)
6. [成功获取后] _start_scanner_if_needed(pool):
   → 原子 CAS topology->scanner_state (OFF → BG_RUNNING), 仅首个线程启动后台监控
```

**`mongoc_client_pool_push()` (归还)**:
```
1. [无锁] 重置 client 的 sockettimeoutms 为默认值 (300s) 或 URI 配置值
   — 防止上一个使用者通过 set_sockettimeoutms() 修改的超时设置泄漏
2. lock(pool->mutex)
3. 获取当前 topology description 快照，对比 pool->last_known_serverids
   — 若 server ID 集合发生变化 (replica set 成员变更):
     a. 更新 pool->last_known_serverids 为新集合
     b. 遍历队列中所有已缓存的 client (prune_client)，断开到已移除 server 的连接
4. 对本次归还的 client 执行 prune_client — 同样断开到已移除 server 的连接
5. _mongoc_queue_push_head() 将 client 插入队列头部 (LIFO)
6. mongoc_cond_signal() 唤醒一个在 pop() 中等待的线程
7. unlock(pool->mutex)
```

**`mongoc_client_pool_try_pop()` (非阻塞获取)**:
- 与 pop 类似，但 queue 为空且 size >= max_pool_size 时直接返回 NULL，不阻塞。
- 成功获取 client 后同样调用 `_start_scanner_if_needed()` 启动后台监控。

### 1.5 后台线程

Pool 首次 `pop()` 成功后调用 `_start_scanner_if_needed()` → `_mongoc_topology_background_monitoring_start()`，后者通过原子 CAS (`scanner_state`: OFF → BG_RUNNING) 确保所有后台监控线程只启动一次:

| 线程 | 数量 | 启动方式 | 功能 |
|------|------|---------|------|
| Server monitor threads | 每 server 一个 | `mongoc_server_monitor_run()` 创建 | 心跳检测 (SDAM)，周期性向各 server 发送 `hello` 命令，同时扫描连接健康状态 |
| RTT monitor threads | 每 server 一个 | `mongoc_server_monitor_run_as_rtt()` 创建 | 往返时间测量，用于驱动 nearest 读偏好 |
| SRV polling thread | 1 个 | `mcommon_thread_create(srv_polling_run)` 创建 | 周期性重新解析 SRV DNS 记录 (仅 `mongodb+srv://`) |

所有后台线程的启停由 `mongoc_topology_t` 内的 `scanner_state` 原子变量统一管理（状态机: OFF → BG_RUNNING → OFF）。这些线程对调用者完全透明，无需应用层管理。

### 1.6 生命周期契约

```
main():
    mongoc_init()          // 全局一次 (C 层: 线程安全，内部用 bson_once)
    // C++ wrapper: MongoSystem::Instance().Initialize()
    
    创建线程:
        client = mongoc_client_pool_pop(pool)   // 线程安全
        // 使用 client (阻塞操作: find/insert/update...)
        mongoc_client_pool_push(pool, client)   // 线程安全
    
    join 所有线程
    mongoc_client_pool_destroy(pool)  // 非线程安全！必须在 join 之后
    // C++ wrapper: pool->Destroy(); delete pool;
    mongoc_cleanup()                  // 全局一次
    // C++ wrapper: MongoSystem::Instance().Shutdown()
```

### 1.7 组件线程安全速查表

| 组件 | 线程安全 | 机制 |
|------|---------|------|
| `mongoc_init` / `mongoc_cleanup` | 是 | `bson_once` 保证只执行一次 |
| `mongoc_client_pool_pop/push/try_pop` | 是 | `bson_mutex_t` + `mongoc_cond_t` |
| `mongoc_client_pool_set_*` | 是 (仅首次 pop 前) | `client_initialized` 标志位 + mutex |
| `mongoc_client_pool_destroy` | **否** | 必须 join 所有线程后调用 |
| `mongoc_client_t` (所有操作) | **否** | 无内部锁, 每线程独占 |
| `mongoc_cursor_t` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_database_t` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_collection_t` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_client_session_t` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_gridfs_*` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_client_encryption_t` | **否** | 纯数据聚合结构体，无内部锁 |
| `mongoc_server_session_pool` (内嵌于 topology) | 是 | `bson_mutex_t` (LIFO 空闲 session 栈) |
| Topology 状态修改 | 是 | `bson_mutex_t` (tpld_modification_mtx + srv_polling_mtx) |
| Topology Description 读取 | 是 (共享锁) | `mc_shared_tpld` 原子引用计数 + 全局 `bson_shared_mutex_t` (共享模式, 多读者并发) |

### 1.8 Topology Description 共享指针机制

Topology 的线程安全设计分两层:

**修改层** (`tpld_modification_mtx` + `srv_polling_mtx`):
- `mongoc_topology_t` 使用两个独立的 `bson_mutex_t`（**不是** `bson_shared_mutex_t` / 读写锁）保护内部可变状态
- `tpld_modification_mtx`: 保护 topology description 的修改 (server 发现、状态变更等)，也用于 `cond_client` 的条件等待
- `srv_polling_mtx`: 保护 SRV DNS 轮询线程的状态和 `srv_polling_cond`

**读取层** (`mc_shared_tpld` + 全局读写锁):
- `mc_shared_tpld` 是一个 union，包裹 `mongoc_shared_ptr`（引用计数指针）指向 `mongoc_topology_description_t`
- `mongoc_topology_description_t` 自身是一个**纯数据结构**（无锁、无引用计数），不可变 (immutable)
- 引用计数由 `mongoc_shared_ptr` 的 `_aux` 辅助结构管理，增/减引用计数使用 `mcommon_atomic_int_fetch_add/sub`（无锁原子操作）
- **关键**: 共享指针本身的 load/store 操作使用**全局** `bson_shared_mutex_t g_shared_ptr_mtx`（读写锁）保护：
  - `mongoc_atomic_shared_ptr_load()` 持共享锁（读锁），允许多个读线程并发获取 topology description 快照
  - `mongoc_atomic_shared_ptr_store()` 持独占锁（写锁），修改时阻塞所有读者

```
读取路径 (共享锁, 允许多读者并发):
  bson_shared_mutex_lock_shared(&g_shared_ptr_mtx)  // 全局共享锁
  → mongoc_atomic_shared_ptr_load()                   // 原子 incref
  → bson_shared_mutex_unlock_shared(&g_shared_ptr_mtx)
  → 返回 description 快照 (不可变, 无需继续持锁)
  → 用完后 mc_tpld_drop_ref() → 原子 decref

修改路径 (独占锁 + tpld_modification_mtx):
  lock(tpld_modification_mtx)
  → copy-on-write: 创建新 description 深拷贝
  → bson_shared_mutex_lock(&g_shared_ptr_mtx)        // 全局独占锁
  → mongoc_atomic_shared_ptr_store()                  // 原子替换指针
  → bson_shared_mutex_unlock(&g_shared_ptr_mtx)
  → cond_broadcast(&cond_client)                      // 唤醒等待的 client 线程
  → unlock(tpld_modification_mtx)
```

**注意**: 虽然使用了全局读写锁，但临界区极短（仅指针 load/store，不含 description 的深拷贝），且读操作持共享锁允许多读者并发。这与常见的 per-object 读写锁在性能特性上有本质区别——锁竞争只发生在指针交换瞬间，而非整个 topology 查询期间。

这种设计的优势: 高频的 topology 读取 (每个 client 操作都需要查询 server 地址) 只在获取/释放共享指针快照时短暂持锁（纳秒级），之后的整个查询过程中持有的是不可变的 description 快照，完全无锁。

### 1.9 服务端 Session 池

`mongoc_topology_t` 内嵌一个线程安全的 `mongoc_server_session_pool`（由 `MONGOC_DECL_SPECIAL_TS_POOL` 宏生成），为 pool 中所有 client 提供隐式会话共享:

**内部结构** (`mongoc-ts-pool.c`):
```c
struct mongoc_ts_pool {
    mongoc_ts_pool_params params;  // init/destroy/prune 回调
    pool_node *head;               // LIFO 空闲 session 链表头
    int32_t size;                  // 原子读取, 无需锁
    bson_mutex_t mtx;              // 保护 head 链表操作
};
```

**操作**:
- `mongoc_server_session_pool_get()`: 从池中 LIFO pop 一个 session，若为空则创建新 session
- `mongoc_server_session_pool_return()`: 检查 prune 条件后 push 回池（LIFO）
- `mongoc_server_session_pool_drop()`: 从池中永久移除一个 session

**Prune 条件** (`_server_session_should_prune`):
- `dirty` session（遇到过网络错误）→ 丢弃
- 从未使用过的 session（`last_used_usec == SESSION_NEVER_USED`）→ 丢弃
- Load-balanced topology 中永不 prune
- 其他: `last_used + topology.session_timeout_minutes × 60s < now - 1min` → 超时丢弃

**线程安全**: pool 的 `bson_mutex_t` 保护 push/pop，`size` 字段支持原子无锁读取（`mongoc_ts_pool_size()` / `mongoc_ts_pool_is_empty()`）。

**关键**: `mongoc_client_session_t`（从 pool 取出包装后的 session 对象）本身**没有内部锁**，不是线程安全的。一个 session 只应由一个线程使用。但底层的 `mongoc_server_session_pool`（存储和复用原始 server session）是线程安全的。

---

## 2. 现有项目封装分析

项目在 `src/runtime/database/mongo/` 下已有完整的 C++ RAII 封装层 (`engine::mongo` namespace):

### 2.1 现有架构

```
MongoSystem               (mongo_system.h/.cc)
  └─ Singleton: 管理 mongoc_init() / mongoc_cleanup() 全局生命周期
  └─ 必须在所有 mongo 操作之前初始化, 之后关闭

MongoInit                (mongo_init.h/.cc)
  └─ 静态工具类: Init() / Cleanup() — MongoSystem 的替代初始化方式
  └─ 内部调用 mongoc_init() / mongoc_cleanup()

MongoClientPool          (mongo_client_pool.h/.cc)
  └─ 封装 mongoc_client_pool_t
  └─ Pop() / Push() / TryPop() — 标准 pool 操作
  └─ 静态 New() 工厂 + Destroy(); 禁止拷贝/移动
  └─ SetMaxSize(), SetAppname(), SetApmCallbacks(), SetServerApi() 等配置方法

MongoClient              (mongo_client.h/.cc)
  └─ 封装 mongoc_client_t
  └─ owned 标志区分: 独立 client vs pool 借出的 client
  └─ FromPooled() / ReleaseFromPool() — pool 生命周期管理
  └─ 所有 CRUD 操作: CommandSimple, FindWithOpts, InsertOne, UpdateOne...

MongoDatabase            (内嵌于 mongo_client.h/.cc)
MongoCollection          (内嵌于 mongo_client.h/.cc)
MongoCursor, MongoSession, MongoBulkOperation, MongoChangeStream ...
MongoUri                 (mongo_uri.h/.cc) — 值类型，支持移动语义
```

### 2.2 现有封装的特点

- Pimpl 惯用法 (`std::unique_ptr<Impl>`) — 大多数类采用此模式
- 禁止拷贝，允许移动 (move-only) — 多数类的默认设计
- 值类型（无 Pimpl，栈分配）: `BsonDocument` (128B inline), `BsonIter` (160B inline), `MongoError` (512B inline), `MongoOid` (12B), `MongoDecimal128` (16B), `MongoUri` (Pimpl + move), `MongoOptional` (copyable)
- `MongoSystem` 是 singleton，`MongoInit` 是静态工具类——两者都管理 `mongoc_init()` / `mongoc_cleanup()` 的全局生命周期
- `MongoClient::owned` 标志位 + `ReleaseFromPool()` 区分独立 client 和 pool 借出的 client (析构时 pool client 不调用 `mongoc_client_destroy`)
- 所有 MongoDB 操作都是**同步阻塞**的（这正是用户需要的行为）

### 2.3 现有封装存在的不足

1. **没有线程安全的 RAII guard**: 使用者需要手动 Pop/Push，容易忘记归还
2. **没有 per-thread client 缓存**: 每次操作都要 Pop/Push，高并发下对 pool mutex 竞争大
3. **没有连接池参数配置指导**: maxPoolSize、waitQueueTimeoutMS 等关键参数没有文档
4. **Pool 配置与 Pop 之间的竞态**: `SetMaxSize`、`SetAppname` 等配置只能在首次 Pop 前设置，但现有 API 没有强制顺序

---

## 3. 多线程阻塞 Client 方案设计

### 3.1 设计目标

1. 每个线程独立持有 `mongoc_client_t`，所有操作**同步阻塞**
2. 线程通过共享的 `MongoClientPool` 获取和归还 client
3. RAII guard 自动管理 client 借还，防止泄漏
4. 支持 per-thread client 缓存以降低 pool 竞争
5. 清晰的生命周期管理

### 3.2 核心方案: ThreadPoolClientGuard (RAII)

```cpp
// 新增: src/runtime/database/mongo/mongo_client_guard.h

namespace engine {
namespace mongo {

// RAII guard: 构造时从 pool 借 client, 析构时归还。
// 线程安全: 每个 guard 实例应在单个线程内使用。
class MongoClientGuard {
public:
    // 从 pool 阻塞获取 client (可阻塞)
    explicit MongoClientGuard(MongoClientPool& pool);

    // 从 pool 非阻塞获取 client, 若不可用则返回空 guard
    static MongoClientGuard TryPop(MongoClientPool& pool);

    ~MongoClientGuard();

    // 禁止拷贝, 允许移动
    MongoClientGuard(const MongoClientGuard&) = delete;
    MongoClientGuard& operator=(const MongoClientGuard&) = delete;
    MongoClientGuard(MongoClientGuard&& other) noexcept;
    MongoClientGuard& operator=(MongoClientGuard&& other) noexcept;

    // 访问内部 client
    MongoClient* operator->()       { return client_; }
    MongoClient* get()              { return client_; }
    const MongoClient* operator->() const { return client_; }
    const MongoClient* get() const        { return client_; }
    explicit operator bool() const  { return client_ != nullptr; }

private:
    MongoClientPool* pool_ = nullptr;  // 非 owning
    MongoClient*     client_ = nullptr;
};

} // namespace mongo
} // namespace engine
```

### 3.3 使用示例

#### 基本多线程模式

```cpp
#include "runtime/database/mongo/mongo_client_guard.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_system.h"

#include <thread>
#include <vector>

void worker_thread(engine::mongo::MongoClientPool& pool, int thread_id) {
    for (int i = 0; i < 100; ++i) {
        // RAII: Pop → 使用 → Push (自动)
        engine::mongo::MongoClientGuard guard(pool);
        if (!guard) {
            // 超时或 pool 已关闭
            return;
        }

        // 所有 CRUD 操作都是阻塞的, 在当前线程同步执行
        auto* db = guard->GetDatabase("test_db");
        auto* coll = guard->GetCollection("test_db", "test_coll");

        // 同步查询
        engine::mongo::BsonDocument filter;
        filter.AppendInt32("_id", i);
        engine::mongo::MongoCursor* cursor =
            coll->FindWithOpts(filter, nullptr, nullptr);
        // 遍历 cursor ...
        cursor->Destroy();

        coll->Destroy();
        db->Destroy();

    } // guard 析构自动 Push 回 pool
}

int main() {
    engine::mongo::MongoSystem::Instance().Initialize();

    engine::mongo::MongoUri uri("mongodb://localhost:27017");
    auto* pool = engine::mongo::MongoClientPool::New(uri);

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker_thread, std::ref(*pool), i);
    }

    for (auto& t : threads) t.join();

    pool->Destroy();
    delete pool;

    engine::mongo::MongoSystem::Instance().Shutdown();
    return 0;
}
```

#### Per-thread Client 缓存 (降低竞争)

```cpp
// 对于高频访问场景, 使用 thread_local 缓存 client
// 避免每次操作都去 pool Pop/Push (竞争 mutex)

thread_local engine::mongo::MongoClient* tls_client = nullptr;

engine::mongo::MongoClient* get_thread_client(
    engine::mongo::MongoClientPool& pool)
{
    if (tls_client == nullptr) {
        tls_client = pool.Pop();  // blocks per waitQueueTimeoutMS
    }
    return tls_client;
}

void return_thread_client(engine::mongo::MongoClientPool& pool) {
    if (tls_client != nullptr) {
        pool.Push(tls_client);
        tls_client = nullptr;
    }
}

// 使用示例
void high_freq_worker(engine::mongo::MongoClientPool& pool) {
    auto* client = get_thread_client(pool);
    if (!client) return;

    for (int i = 0; i < 10000; ++i) {
        // 直接使用同一 client, 无 pool 竞争
        auto* db = client->GetDatabase("test_db");
        // ...
    }

    return_thread_client(pool);  // 线程结束前归还
}
```

### 3.4 连接池参数配置建议

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| `maxPoolSize` | `std::thread::hardware_concurrency() * 2` | 略大于线程数, 允许一定弹性 |
| `waitQueueTimeoutMS` | `5000` (5秒) | 阻塞 Pop 的超时, 防止死等。**注意**: mongo-c-driver 默认值为 `-1` (无限等待), 建议显式设置 |
| `socketTimeoutMS` | `0` (无限) | 阻塞模式下推荐无限, 由业务层控制超时 |

**关于 `minPoolSize`**: mongo-c-driver 已弃用并移除该参数 (CDRIVER-2390)。Pool 始终从空队列开始，按需创建 client。

**关于 `maxIdleTimeMS`**: mongo-c-driver **不支持此参数**（`MONGOC_URI_MAXIDLETIMEMS` 已被移除）。连接的空闲超时由服务端 `logicalSessionTimeoutMinutes` 通过 SDAM 心跳自动获取，客户端无需也无法配置。maxIdleTimeMS 是 Java/Python driver 的概念，C driver 无等价选项。

URI 示例:
```
mongodb://localhost:27017/?maxPoolSize=16&waitQueueTimeoutMS=5000
```

### 3.5 生命周期严格顺序

```
┌─────────────────────────────────────────────────────┐
│  1. MongoSystem::Instance().Initialize()             │
│     (调用 mongoc_init(), 程序启动全局一次)             │
├─────────────────────────────────────────────────────┤
│  2. MongoClientPool::New()   (创建 pool, 配置各项参数)│
│     ├─ SetMaxSize()                                 │
│     ├─ SetAppname()                                 │
│     └─ SetApmCallbacks()  (必须在首次 Pop 之前!)     │
├─────────────────────────────────────────────────────┤
│  3. 启动工作线程                                     │
│     ┌──────────────────────────────────────────┐    │
│     │ Thread 1:           Thread N:             │    │
│     │   Pop() → CRUD → Push()                   │    │
│     │   Pop() → CRUD → Push()                   │    │
│     │   ...                 ...                 │    │
│     └──────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────┤
│  4. join 所有线程 (必须!)                            │
├─────────────────────────────────────────────────────┤
│  5. pool->Destroy()       (单线程调用)               │
│  6. delete pool            (释放 C++ wrapper)        │
├─────────────────────────────────────────────────────┤
│  7. MongoSystem::Instance().Shutdown()               │
│     (调用 mongoc_cleanup(), 程序退出全局一次)          │
└─────────────────────────────────────────────────────┘
```

### 3.6 架构图

```
                          ┌──────────────────────┐
                          │   MongoClientPool    │
                          │  (线程安全, mutex)    │
                          │                      │
                          │  queue: [C1][C2][C3] │── 空闲 client LIFO 队列
                          │  topology ◄──────────│── 共享集群拓扑 + SDAM 后台线程
                          │  max_size: 16        │
                          │  cond_var            │── 阻塞 Pop 的信号量
                          └──────┬───────────────┘
                                 │
              Pop() / Push()     │     Pop() / Push()
                 ┌───────────────┼───────────────┐
                 ▼               ▼               ▼
          ┌──────────┐    ┌──────────┐    ┌──────────┐
          │ Thread 1 │    │ Thread 2 │    │ Thread N │
          │          │    │          │    │          │
          │ Client*  │    │ Client*  │    │ Client*  │
          │ (独占)    │    │ (独占)    │    │ (独占)    │
          │          │    │          │    │          │
          │ find()   │    │ update() │    │ insert() │
          │ 阻塞同步  │    │ 阻塞同步  │    │ 阻塞同步  │
          └──────────┘    └──────────┘    └──────────┘
```

### 3.7 方案要点总结

| 要点 | 说明 |
|------|------|
| **阻塞模型** | mongoc 所有 API 都是同步阻塞的, 无需异步框架 |
| **每线程独占 Client** | `mongoc_client_t` 非线程安全, 必须每线程一个 |
| **Pool 管理 Client** | `mongoc_client_pool_t` 内部有 mutex, Pop/Push 线程安全 |
| **RAII Guard** | `MongoClientGuard` 自动管理借还, 防止泄漏 |
| **Per-thread 缓存** | 高频场景用 `thread_local` 避免每次 Pop/Push 的锁竞争 |
| **共享 Topology** | 所有 client 共享同一个 `mongoc_topology_t`, 后台心跳/SDAM 线程由 driver 内部管理 |
| **配置时序** | Pool 的 `SetAppname`、`SetApmCallbacks` 等必须在首次 Pop 前调用 |
| **销毁时序** | `pool->Destroy()` 非线程安全, 必须在 join 所有线程后单线程调用 |

### 3.8 推荐新增文件

| 文件 | 说明 |
|------|------|
| `src/runtime/database/mongo/mongo_client_guard.h` | `MongoClientGuard` RAII 类声明 |
| `src/runtime/database/mongo/mongo_client_guard.cc` | `MongoClientGuard` 实现 |

### 3.9 mongo-c-driver 内部线程池 vs 本方案

值得注意的是, mongo-c-driver 的 `mongoc_client_pool_t` 是一个 **client 对象池** 而非传统的连接池或线程池:
- 池中每个 `mongoc_client_t` 内部有自己的 `mongoc_cluster_t`, 其中维护到各 MongoDB server 的 socket 连接
- 所有 client 共享 `mongoc_topology_t`, 后者管理 SDAM 后台线程
- 本方案中每个应用线程对应一个 client (从 pool 借出), 应用层无需自行管理 socket 连接

这与 Java/Python MongoDB driver 的 `MongoClient` (内部自带连接池) 不同——C driver 需要业务层通过 pool 模式自行管理多线程访问。
