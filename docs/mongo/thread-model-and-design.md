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

**`mongoc_client_t` 不是线程安全的**, 不可多线程共享。其内部 `mongoc_cluster_t` 管理 socket 连接池，所有读写操作直接访问 cluster 而**没有任何互斥锁保护**。因此每个线程必须独占一个 `mongoc_client_t`。

### 1.2 线程原语层

mongo-c-driver 在 `src/common/src/common-thread-private.h` 提供了跨平台线程抽象:

| 原语 | POSIX | Windows | 用途 |
|------|-------|---------|------|
| `bson_mutex_t` | `pthread_mutex_t` | `CRITICAL_SECTION` | 互斥锁 |
| `bson_shared_mutex_t` | `pthread_rwlock_t` | `SRWLOCK` | 读写锁 |
| `bson_thread_t` | `pthread_t` | `HANDLE` | 线程句柄 |
| `bson_once_t` | `pthread_once_t` | `INIT_ONCE` | 一次性初始化 |
| `mongoc_cond_t` | `pthread_cond_t` | `CONDITION_VARIABLE` | 条件变量 |

**注意**: `bson_shared_mutex_t` 存在于原语层，但仅用于 `mongoc-scram.c`、`mongoc-oidc-cache.c` 和 `mongoc-shared.c` 的共享指针实现中。**Topology 不使用读写锁**——其内部状态保护使用两个独立的 `bson_mutex_t`（`srv_polling_mtx` + `tpld_modification_mtx`），而 topology description 的跨线程共享通过引用计数原子指针 (`mc_shared_tpld`) 实现无锁读取。

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

1. 所有 pool 中的 client 共享同一个 `mongoc_topology_t`，其中包含 SDAM (Server Discovery and Monitoring) 状态、后台监控线程等。
2. `client_initialized` 标志位在首次 `pop()` 成功后设置为 true，之后 `set_server_api`、`set_structured_log_opts`、`set_oidc_callback` 等 API 将被阻止。
3. `last_known_serverids` 是一个已排序的 `uint32_t` 数组，在 `push()` 归还 client 时，pool 将其与当前 topology 中已知的 server id 集合对比，清理不属于当前集群拓扑的连接。
4. **`min_pool_size` 不存在**——该参数已被 mongo-c-driver 弃用并移除 (CDRIVER-2390)。Pool 始终从空队列开始，按需创建 client。

### 1.4 Pool 操作流程

**`mongoc_client_pool_pop()` (阻塞获取)**:
```
1. lock(pool->mutex)
2. if queue 非空 → pop LIFO 队列 → 返回 client
3. if size < max_pool_size → 创建新 client → size++ → 返回
4. otherwise → cond_wait(&pool->cond, &pool->mutex)  // 阻塞等待
   - 若设置了 waitQueueTimeoutMS > 0 → cond_timedwait → 超时返回 NULL
   - waitQueueTimeoutMS 默认值: -1 (无限等待)
5. unlock(pool->mutex)
```

**`mongoc_client_pool_push()` (归还)**:
```
1. lock(pool->mutex)
2. 重置 client 的 socket 超时状态
3. 对比 last_known_serverids: 遍历 client 内部连接, 清理不在当前
   集群拓扑中的 server 连接 (防止连接到已移除的 replica set 成员)
4. push client 到 queue 头部 (LIFO)
5. cond_signal() 唤醒一个等待线程
6. unlock(pool->mutex)
```

**`mongoc_client_pool_try_pop()` (非阻塞获取)**:
- 与 pop 类似，但 queue 为空且 at capacity 时直接返回 NULL，不阻塞。

### 1.5 后台线程

Pool 首次 `pop()` 时会通过原子 CAS 确保只启动一次 scanner 线程:

| 线程 | 启动方式 | 功能 |
|------|---------|------|
| SRV polling thread | topology 初始化时按需创建 | 周期性重新解析 SRV DNS 记录 |
| Server monitor threads | topology 连接每个 server 时创建 | 每个 server 一个，心跳检测 (SDAM) |
| Connection scanner | 首次 pop 时原子 CAS 确保只启动一次 | 扫描空闲连接、执行健康检查 |

这些后台线程由 pool/topology 管理，对调用者透明。其中 server monitor 和 SRV polling 线程在 `mongoc_topology_t` 中创建和管理。

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
| `mongoc_cursor_t` | **否** | 派生自 client |
| `mongoc_database_t` | **否** | 派生自 client |
| `mongoc_collection_t` | **否** | 派生自 client |
| `mongoc_gridfs_*` | **否** | 文档明确标注 |
| `mongoc_client_encryption_t` | **否** | 文档明确标注 |
| Topology 状态修改 | 是 | `bson_mutex_t` (srv_polling_mtx + tpld_modification_mtx) |
| Topology Description 读取 | 是 (无锁) | `mc_shared_tpld` 原子引用计数共享指针 |

### 1.8 Topology Description 共享指针机制

Topology 的线程安全设计分两层:

**修改层** (`tpld_modification_mtx` + `srv_polling_mtx`):
- `mongoc_topology_t` 使用两个独立的 `bson_mutex_t`（**不是** `bson_shared_mutex_t` / 读写锁）保护内部可变状态
- `tpld_modification_mtx`: 保护 topology description 的修改 (server 发现、状态变更等)
- `srv_polling_mtx`: 保护 SRV DNS 轮询相关的状态

**读取层** (`mc_shared_tpld` 无锁共享):
- `mc_shared_tpld` 是一个引用计数原子共享指针，包裹 `mongoc_topology_description_t`
- 读操作通过 `mongoc_atomic_shared_ptr_load()` 获取当前 description 的快照，**无需获取任何锁**
- 修改操作在 `tpld_modification_mtx` 保护下创建新的 description，然后通过原子 store 替换旧指针
- `mongoc_cond_t cond_client` 条件变量通知等待者 topology 变更

```
读取路径 (无锁):
  mc_tpld_take_ref(&topology)           // 原子 load + incref
  → 使用 description 快照
  → mc_tpld_drop_ref(&topology)         // 原子 decref

修改路径 (持锁):
  lock(tpld_modification_mtx)
  → 创建新 description (copy-on-write)
  → 原子 store 新指针
  → cond_broadcast(&cond_client)        // 通知等待者
  → unlock(tpld_modification_mtx)
```

这种设计的优势: 高频的 topology 读取 (每个 client 操作都需要查询 server 地址) 不会与低频的 topology 修改 (SDAM 心跳更新) 产生锁竞争。

---

## 2. 现有项目封装分析

项目在 `src/runtime/database/mongo/` 下已有完整的 C++ RAII 封装层 (`engine::mongo` namespace):

### 2.1 现有架构

```
MongoSystem               (mongo_system.h/.cc)
  └─ Singleton: 管理 mongoc_init() / mongoc_cleanup() 全局生命周期
  └─ 必须在所有 mongo 操作之前初始化, 之后关闭

MongoClientPool          (mongo_client_pool.h/.cc)
  └─ 封装 mongoc_client_pool_t
  └─ Pop() / Push() / TryPop() — 标准 pool 操作
  └─ 构造时传 MongoUri, 析构时 destroy pool

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

- Pimpl 惯用法 (`std::unique_ptr<Impl>`)
- 禁止拷贝/移动 (大部分类)
- `MongoUri` 是值类型，支持移动语义（唯一的例外）
- `MongoSystem` 是 singleton，管理 `mongoc_init()` / `mongoc_cleanup()` 的全局生命周期
- `MongoClient::owned` 标志位区分独立 client 和 pool client (析构时 pool client 不调用 `mongoc_client_destroy`)
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
    MongoClient* operator->()       { return client_.get(); }
    MongoClient* get()              { return client_.get(); }
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
        filter.Append("_id", i);
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
| `maxIdleTimeMS` | `60000` (60秒) | 闲置连接回收, 需 MongoDB 4.0+ |
| `socketTimeoutMS` | `0` (无限) | 阻塞模式下推荐无限, 由业务层控制超时 |

**关于 `minPoolSize`**: mongo-c-driver 已弃用并移除该参数 (CDRIVER-2390)。Pool 始终从空队列开始，按需创建 client，因此不应在 URI 中设置 `minPoolSize`。

URI 示例:
```
mongodb://localhost:27017/?maxPoolSize=16&waitQueueTimeoutMS=5000&maxIdleTimeMS=60000
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
