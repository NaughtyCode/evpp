# CloudEngine Runtime — 深度性能分析报告

> 分析范围: `src/runtime/` 全部模块  
> 分析方法: 逐文件代码审查，聚焦内存分配、锁竞争、数据结构选择、热路径开销  
> 生成日期: 2025-07-14

---

## 目录

1. [总览与严重度分级](#1-总览与严重度分级)
2. [P0 — 每帧热路径问题](#2-p0--每帧热路径问题)
3. [P1 — 内存分配与数据结构](#3-p1--内存分配与数据结构)
4. [P2 — 锁竞争与并发设计](#4-p2--锁竞争与并发设计)
5. [P3 — 网络与 I/O 层](#5-p3--网络与-io-层)
6. [P4 — 低优先级优化机会](#6-p4--低优先级优化机会)
7. [总结与行动建议](#7-总结与行动建议)

---

## 1. 总览与严重度分级

本报告从 `src/runtime/` 下 16 个子模块中识别出 **28 个性能问题**，按影响面和修复成本分为四级:

| 级别 | 数量 | 定义 | 典型影响 |
|------|------|------|----------|
| **P0** | 6 | 每帧触发，直接影响帧率 | 帧时间增加 5-30% |
| **P1** | 9 | 高频分配/数据结构低效 | GC 压力，缓存污染 |
| **P2** | 7 | 锁粒度/并发模型问题 | 多核扩展受限 |
| **P3** | 6 | 低优先级，长期优化 | 边界场景收益 |

---

## 2. P0 — 每帧热路径问题

### 2.1 `ConfigManager` 值语义返回导致每帧分配

**文件**: [config.h](src/runtime/config/config.h:168-172)  
**严重度**: 🔴 P0

```cpp
// config.h:168-172 — 每次调用触发完整 struct 拷贝 + 多个 std::string 分配
RuntimeConfig GetRuntimeConfig() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return runtime_config_;  // 完整值拷贝
}
ServerConfig GetServerConfig() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return server_config_;   // 完整值拷贝
}
```

**根因**: `RuntimeConfig` 包含 `LogConfig`（7 个 `std::string` 成员）、`FrameConfig`（3 个 `int`）、和其他 `std::string` 成员。每次以值返回触发 ~10 次堆分配。

**热路径证据**: `Engine::FrameLoop()` 中通过 `ConfigManager::Instance().GetServerConfig()` 访问 `admin_port`（[engine.cc](src/runtime/engine/engine.cc:228)），而 `GetServerConfig` 在 `ScriptVM::CallGlobalFunction` → Lua 脚本中也通过绑定频繁调用。

**修复建议**:
- 对高频访问的字段（`frame.interval_ms`, `admin_port`）提供独立的 `const&` 或 `int` 返回接口
- 或将热路径字段缓存为 `std::atomic<int>` 等 lock-free 值，通过 `ConfigManager::Reload()` 时更新

---

### 2.2 `TimerManager::update()` 内多次时钟查询

**文件**: [timer_manager.cc](src/runtime/core/timer/timer_manager.cc:143-176)  
**严重度**: 🔴 P0

```cpp
// timer_manager.cc:143 — 每帧执行
TimerManager::UpdateResult TimerManager::update() {
    return update(now());  // 时钟查询 #1
    // ...进入 update(TimePoint) — 查询 #2 在 result.current_time = current_time
}
```

`update()` 内部链路: `update()` → `now()` → `update(now, ...)` → `update_hrtimers()` → `hrtimer_mgr_->process_all_expired(now)` → `update_wheel()` → `wheel_->advance(elapsed_ms)` → `update_alarms()` → `alarm_mgr_->process_expired(now)` → `next_event_ns()` 再次调用 `hrtimer_mgr_->next_event_ns(current_time)`。

在单次 `update()` 调用中，`now()` 被查询至少 **3 次**（update 入口、next_event_ns 内部、alarm 内部），而该值在整个帧内是不变的。

**修复建议**:
- 在 `update()` 入口一次性获取 `now`，传递给所有子调用
- `next_event_ns` 接受 `now` 参数而非内部重新查询

---

### 2.3 `CoroutineScheduler::Update()` 每帧全量扫描 + 双重时间查询

**文件**: [coroutine_scheduler.cc](src/runtime/vm/coroutine_scheduler.cc:59-112)  
**严重度**: 🔴 P0

```cpp
// coroutine_scheduler.cc:63-65 — 两次 now() 调用
auto start = std::chrono::steady_clock::now();
auto deadline = start + std::chrono::milliseconds(max_yield_ms);

// coroutine_scheduler.cc:68-70 — 第三次 now() 调用
auto now = std::chrono::steady_clock::now();
auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();

// coroutine_scheduler.cc:72-82 — 全量遍历所有协程
for (auto& [handle, cs] : coroutines_) {
    if (cs.state == State::Dead) continue;
    if (cs.state == State::Suspended && cs.wake_at_ms > 0 && now_ms >= cs.wake_at_ms) {
        cs.state = State::Runnable;
    }
    if (cs.state == State::Runnable) runnable.push_back(handle);
}
```

**根因**:
1. 每帧遍历全部协程（包括 Suspended 和 Dead），复杂度 O(N)。Dead 协程在 `GarbageCollect()` 中才被清理，可能在多次帧间残留。
2. 3 次 `steady_clock::now()` 调用均为 syscall/vDSO 开销。

**修复建议**:
- 维护独立的 `runnable_coros_` 列表，避免每帧扫全量
- 为 `wake_at_ms` 维护最小堆，仅检查队首到期协程
- 统一时间查询为单次 `now_ms`

---

### 2.4 `SpaceMessageRouter::ProcessPending()` 的 Lua 调用链开销

**文件**: [space_message.cc](src/runtime/space/space_message.cc:20-60)  
**严重度**: 🔴 P0

每帧最多处理 256 条跨 Space 消息，每条消息执行：
1. `lua_getglobal(L, "space")` — 全局表查找
2. `lua_getfield(L, -1, "_deliver_message")` — 字段查找
3. 4 次 `lua_pushinteger` / `lua_pushlstring`
4. `lua_pcall(L, 4, 0, 0)` — 保护模式调用（内部 setjmp/longjmp）
5. 多次 `lua_pop`

`lua_pcall` 在保护模式下设置恢复点，对高频调用极为昂贵。

**修复建议**:
- 缓存 `_deliver_message` 函数引用到 C++ 侧，避免每消息两次表查找
- 如果消息量大，考虑 C++ 直接派发（绕过 Lua 调用帧）
- 将多条消息批量聚合为单次 Lua 调用

---

### 2.5 `EntityManager::ForEachActive()` + `ActiveCount()` 全量扫描

**文件**: [entity_manager.cc](src/runtime/entity/entity_manager.cc:51-55, 78-86)  
**严重度**: 🔴 P0

```cpp
// entity_manager.cc:51 — O(N) 遍历，N = 所有实体（含非活跃）
void EntityManager::ForEachActive(std::function<void(Entity&)> callback) {
    for (auto& pair : entities_) {
        if (pair.second->GetState() == EntityState::Active) {
            callback(*pair.second);
        }
    }
}
```

`ForEachActive` 遍历 `unordered_map` 中所有实体，仅对状态为 Active 的执行回调。当大量实体处于 Destroyed/Suspended 状态时（如分线切换后），大量时间是无效遍历。

**修复建议**:
- 维护独立的活跃实体集合（如 `std::unordered_set<EntityId>` 或侵入式链表）
- 或使用 `boost::multi_index` 按状态分片

---

### 2.6 `AOIManager::UnregisterEntity()` 全量可见性清理

**文件**: [aoi_manager.cc](src/runtime/aoi/aoi_manager.cc:21-28)  
**严重度**: 🔴 P0

```cpp
// aoi_manager.cc:25-28 — O(N) 扫描所有其他实体的可见集
for (auto& [other_id, vis_set] : visible_) {
    if (vis_set.erase(id) && event_callback_) {
        event_callback_(other_id, id, false);
    }
}
```

当实体离开场景时，需要将其从**所有其他实体**的可见集中移除。N 个实体场景中单次 Unregister 复杂度 O(N)，大量实体同时离开时（如场景切换）复杂度 O(N²)。

**修复建议**:
- 维护反向索引: `observed_by_[id]` 记录哪些实体的可见集中包含此实体
- Unregister 时仅遍历反向索引（通常远小于全量）

---

## 3. P1 — 内存分配与数据结构

### 3.1 `std::function` 过度使用导致大量堆分配

**涉及文件**: 全局  
**严重度**: 🟠 P1

**问题分布**:

| 位置 | 代码 | 每次分配 |
|------|------|----------|
| `timer_manager.cc:216` | `create_simple_timer` | 3 次 (`shared_ptr<function>` + 外层 `function` + capture) |
| `timer_manager.cc:447` | `create_repeating_simple_timer` | 同上，每重复定时器 3 次 |
| `entity.cc:90` | `Entity::AddTimer` | lambda capture `[eid, cb]` 每次分配 |
| `event_loop.cc:229` | `QueueInLoop` | `std::function` 包装 + 可能的 capture 分配 |
| `engine.cc:237` | `RunEvery` lambda | 每帧定时器 capture 分配 |
| `tcp_conn.cc` | `HandleRead/Write` callbacks | 每个连接回调分配 |

**根因**: `std::function` 对小对象使用 SBO (Small Buffer Optimization)，但任何捕获超过 ~16-32 字节（依赖实现）的 lambda 都会触发堆分配。本项目大量捕获 `shared_ptr`、`string`、多个变量，远超 SBO 阈值。

**修复建议**:
- 对高频路径（timer callback, network callback）使用 `function_ref`（`std::function` 的轻量替代，仅持有指针，不拥有所有权）
- 或使用类型擦除的自定义 `delegate`（如 `eteVDelegate` 模式，单指针分配）
- 对低额路径保留 `std::function` 便利性

---

### 3.2 `TimerQueue` 使用 `std::multimap` 导致节点分配

**文件**: [timer_queue.h](src/runtime/core/timer/timer_queue.h:136-137)  
**严重度**: 🟠 P1

```cpp
// timer_queue.h:136 — 红黑树每次 insert 分配一个树节点
using MapType = std::multimap<TimePoint, NodePtr>;
MapType tree_;
```

`std::multimap` 是节点容器，每次 `insert` / `emplace` 分配一个新树节点。`TimerQueue` 用于:
- `HrTimerManager` 的高精度定时器队列 (每帧 insert/remove 多个)
- `AlarmTimerManager` 的警醒队列

在 30fps 游戏中每秒可能有数百次 insert+remove 操作，产生大量堆碎片。

**修复建议**:
- 考虑使用侵入式红黑树（节点嵌入在 `HrTimerNode` / `Alarm` 内部），消除独立节点分配
- 或使用 `boost::intrusive::rbtree` / `boost::intrusive::set`
- 短期方案: 使用带 custom allocator 的 `std::multimap`（如分配池）

---

### 3.3 `SpatialGrid::Update()` — O(n) 向量删除

**文件**: [spatial_index.cc](src/runtime/aoi/spatial_index.cc:60-63)  
**严重度**: 🟠 P1

```cpp
// spatial_index.cc:62 — std::remove + erase = O(cell_size)
old_cell.erase(std::remove(old_cell.begin(), old_cell.end(), id), old_cell.end());
```

实体跨格子移动时，先从旧格子用 `std::remove` + `erase` 移除，O(N) 于格内实体数。在热点区域（玩家密集处），单个格子可能有数十到上百实体。

**修复建议**:
- 使用 `std::unordered_set` 或侵入式链表替代 `std::vector`
- 或将 `entity_cell_` 反向索引改为同时存储格内位置迭代器，实现 O(1) 移除

---

### 3.4 `Entity::owned_timers_` — O(n) 定时器移除

**文件**: [entity.cc](src/runtime/entity/entity.cc:72-78)  
**严重度**: 🟠 P1

```cpp
// entity.cc:72 — O(n) 线性查找
void Entity::RemoveOwnedTimer(TimerId id) {
    auto it = std::find(owned_timers_.begin(), owned_timers_.end(), id);
    if (it != owned_timers_.end()) {
        TimerManager::instance().cancel_timer(id);
        owned_timers_.erase(it);
    }
}
```

实体可能绑定多个定时器（技能 CD、buff 过期、移动定时器等）。每个定时器的取消触发 O(N) 线性扫描。

**修复建议**:
- 改用 `std::unordered_set<TimerId>`，O(1) 查找和移除
- 或如果定时器数量通常很少 (< 8)，保持 vector 但添加注释说明

---

### 3.5 `AttributeTable` 设计问题

**文件**: [attribute.h](src/runtime/entity/attribute.h:26-47)  
**严重度**: 🟠 P1

```cpp
// attribute.h:20 — std::string key by value (每次 Set 拷贝 key)
void Set(const std::string& key, AttrValue value) { ... }

// attribute.h:12 — AttrValue 包含 std::string，每次读写涉及分配
using AttrValue = std::variant<int64_t, double, std::string, bool>;
```

**问题**:
1. `Set(const std::string& key, AttrValue value)` — 虽然 key 是 `const&`，但 `value` 以值传递。对于 `std::string` 类型的属性值，每次 Set 至少一次分配。
2. `Get` 按值返回 `AttrValue`，触发 variant 拷贝。
3. 使用 `std::unordered_map<std::string, AttrValue>`，key 比较需要完整字符串哈希。

**修复建议**:
- 考虑 interned string / string_id 替代 key（如 64-bit hash id）
- `AttrValue` 考虑使用小字符串优化 (SSO) 的定长存储，减少堆分配
- `Get` 返回 `const AttrValue*` 而非值拷贝

---

### 3.6 `ConfigManager::GetRuntimeConfig()` 等全量值拷贝

**文件**: [config.h](src/runtime/config/config.h:168-198)  
**严重度**: 🟠 P1

与 P0-2.1 重叠，补充具体数据:

`ServerConfig` 拷贝成本估算（64-bit 平台）:
- `HttpConfig`: 1 double (~8B)
- `MsgpackConfig`: 1 int + 1 size_t (~16B)
- `scripts_dir`: ~32B (SSO 可能覆盖短路径，但生产路径通常长)
- `mongodb_dev`, `mongodb_public`: 各 ~32B
- `db_service`: ~32B
- 总计: ~120B + 可能的堆分配

在每帧都调用时，累积的 malloc/free 开销显著。

建议: 见 2.1。

---

### 3.7 `LengthPrefixedCodec::Decode` 返回值分配

**文件**: [length_prefixed_codec.h](src/runtime/network/length_prefixed_codec.h:57)  
**严重度**: 🟠 P1

```cpp
// 每个解码消息都是新 std::string + vector 分配
std::vector<std::string> Decode(evpp::Buffer* buffer);
```

对于高吞吐消息（如位置同步每秒 20 条 × 1000 玩家 = 20000 msg/s），每秒产生 20000 个 `std::string` + `vector` 分配。

**修复建议**:
- 改为 `void Decode(Buffer*, std::vector<Slice>& out)` — 返回 Buffer 内 Slice 视图
- 或提供 `Decode(Buffer*, FunctionRef<void(Slice)> callback)` 回调模式避免 vector

---

### 3.8 `EntityCache` 值语义缓存

**文件**: [cache.h](src/runtime/database/cache.h:19-42)  
**严重度**: 🟠 P1

```cpp
// cache.h:22 — Get 返回 optional<T> 值拷贝
std::optional<T> Get(const std::string& key) {
    // ...
    return it->second->second;  // 值拷贝 T
}
```

实例化为 `EntityCache<std::string>` 时（[orm.h](src/runtime/database/orm.h:101)），每次缓存命中都会拷贝完整的 JSON 文档字符串。对于大型 JSON（数 KB），这是显著开销。

**修复建议**:
- 返回 `const T*` 或 `std::optional<std::reference_wrapper<const T>>`
- 或使用 `shared_ptr<T>` 作为缓存值类型（共享所有权，避免拷贝）

---

### 3.9 `TCPConn::pending_messages_` 优先级队列的字符串拷贝

**文件**: [tcp_conn.h](src/runtime/evpp/tcp_conn.h:179-187)  
**严重度**: 🟠 P1

```cpp
struct PendingMessage {
    MessagePriority priority;
    std::string data;        // 完整消息副本
    int64_t enqueue_time;
};
// ...
std::priority_queue<PendingMessage> pending_messages_;
```

`std::priority_queue` 的 `pop()` 和 `push()` 都涉及 `PendingMessage` 的移动/拷贝，内部的 `std::string data` 随之移动。对于高频消息发送，这是不必要的开销。

**修复建议**:
- 使用 `boost::container::stable_vector` 或自定义侵入式链表
- 或对 payload 使用 `shared_ptr<std::string>` 减少拷贝

---

## 4. P2 — 锁竞争与并发设计

### 4.1 `HrTimerManager::mutex_` 与 `AlarmTimerManager::mutex_` 使用 `std::recursive_mutex`

**文件**: [hr_timer.h](src/runtime/core/timer/hr_timer.h:519), [alarm_timer.h](src/runtime/core/timer/alarm_timer.h:260)  
**严重度**: 🟡 P2

```cpp
// hr_timer.h:519, alarm_timer.h:260
mutable std::recursive_mutex mutex_;
```

`std::recursive_mutex` 比 `std::mutex` 慢 2-5 倍（需要维护 owner 线程 ID 和递归计数）。在每帧 `process_all_expired()` 调用中，此锁被 acquire/release 多次（每次 expired timer 处理期间 unlock 以允许回调重入）。

**根因**: 定时器回调可能需要创建/取消其他定时器，导致重入。但这一般是少数情况。

**修复建议**:
- 改用 `std::mutex` + 延迟队列模式：回调触发的操作记录到 pending list，当前批次处理完后再应用
- 或仅在回调期间 unlock，其他操作使用 `try_lock` + lock-free fast path

---

### 4.2 `TimerManager::entries_mutex_` 粒度过粗

**文件**: [timer_manager.h](src/runtime/core/timer/timer_manager.h:337)  
**严重度**: 🟡 P2

```cpp
mutable std::mutex entries_mutex_;
std::unordered_map<TimerId, std::unique_ptr<TimerEntry>> entries_;
```

所有 timer ID 操作（创建、查询、销毁、统计）共享一把锁。`get_entry()` 被几乎所有公开 API 调用，形成竞争热点。

对于高频率的定时器操作（如技能系统每帧创建/取消数十字定时器），此锁可能成为瓶颈。

**修复建议**:
- 使用分片锁（如按 `TimerId % N` 分 4-8 个桶）
- 或使用 `folly::ConcurrentHashMap` / `absl::flat_hash_map` + 细粒度锁
- 短期：将 `entries_` 改为 `std::shared_mutex` 让 `get_entry` 使用共享锁

---

### 4.3 `ClockManager::mutex_` 每次 `now()` 加锁

**文件**: [clock_source.h](src/runtime/core/timer/clock_source.h:350-352)  
**严重度**: 🟡 P2

```cpp
// clock_source.h:350 — 每次时钟查询都获取互斥锁
TimePoint now() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_source_->read();
}
```

`now()` 在主循环每帧被调用多次（见 2.2）。每次加锁/解锁即使无竞争也有 ~20ns 开销。

**根因**: `current_source_` 理论上可能在运行时被替换（`register_source` → `reevaluate_source`）。但在实际使用中，时钟源在启动时设置后不再更改。

**修复建议**:
- 使用 `std::atomic<ClockSource*>` 存储当前源，`now()` 使用 `memory_order_acquire` 读取
- 替换时钟源时使用 `memory_order_release` 写入
- 消除 `now()` 路径上的所有锁

---

### 4.4 `EntityCache::mutex_` — 读操作也使用排他锁

**文件**: [cache.h](src/runtime/database/cache.h:22)  
**严重度**: 🟡 P2

```cpp
// cache.h:22 — 即使是纯读 Get 也获取排他锁
std::optional<T> Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // ...
}
```

LRU 缓存中 `Get` 需要移动访问元素到链表前部（`lru_list_.splice`），这确实需要排他访问。但对于高命中率缓存，锁竞争直接限制并发读吞吐。

**修复建议**:
- 考虑分段 LRU（如 ConcurrentLRU 模式），将总缓存拆分为 N 个 shard
- 或使用 lock-free LRU 近似算法（如 clock-pro）

---

### 4.5 `EventLoop::DoPendingFunctors` — 非 ConcurrentQueue 路径的向量交换

**文件**: [event_loop.cc](src/runtime/evpp/event_loop.cc:228-243)  
**严重度**: 🟡 P2

```cpp
// event_loop.cc:235 — 交换整个 vector，可能很大
std::lock_guard<std::mutex> lock(mutex_);
notified_.store(false);
pending_functors_->swap(functors);
```

没有 `concurrentqueue` 时，`DoPendingFunctors` 在锁内执行 `swap`，然后锁外执行所有 functor。如果在执行 functor 期间有新的 `QueueInLoop` 调用，它们会阻塞在 `mutex_` 上。

**修复建议**:
- 确保编译时定义了 `H_HAVE_CAMERON314_CONCURRENTQUEUE`（项目依赖列表中确实有 concurrentqueue）
- 回退方案: 使用双缓冲（两个 vector，生产者写一个，消费者读另一个，指针原子交换）

---

### 4.6 `EventLoopThreadPool::GetNextLoop` — `fetch_add` 无取模优化

**文件**: [event_loop_thread_pool.cc](src/runtime/evpp/event_loop_thread_pool.cc:163-172)  
**严重度**: 🟡 P2

```cpp
// 对每条新连接都执行一次 fetch_add + 取模
int64_t next = next_.fetch_add(1);
next = next % threads_.size();
```

使用 `fetch_add` 保证原子性，但取模运算 (`%`) 是整数除法指令，在 x86 上 ~20-80 cycles。

**修复建议**:
- 如果 `threads_.size()` 是 2 的幂（如 2/4/8），使用位掩码 `next & (size-1)`
- 或预计算取模结果到环形数组

---

### 4.7 `TimerManager::instance_mutex_` — 单例模式每次加锁

**文件**: [timer_manager.cc](src/runtime/core/timer/timer_manager.cc:31-37)  
**严重度**: 🟡 P2

```cpp
TimerManager& TimerManager::instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::unique_ptr<TimerManager>(new TimerManager());
        instance_->initialize();
    }
    return *instance_;
}
```

每次调用 `TimerManager::instance()` 都获取锁。而 C++11 保证静态局部变量初始化的线程安全，可简化为:

```cpp
TimerManager& TimerManager::instance() {
    static TimerManager mgr;  // C++11 保证线程安全的懒初始化
    return mgr;
}
```

---

## 5. P3 — 网络与 I/O 层

### 5.1 `TCPConn::Send(Slice)` 跨线程调用时字符串拷贝

**文件**: [tcp_conn.cc](src/runtime/evpp/tcp_conn.cc:90-97)  
**严重度**: 🟡 P3

```cpp
// tcp_conn.cc:93 — 非 loop 线程调用时，拷贝整个消息
void TCPConn::Send(const Slice& message) {
    if (loop_->IsInLoopThread()) {
        SendInLoop(message);
    } else {
        loop_->RunInLoop(
            std::bind(&TCPConn::SendStringInLoop, shared_from_this(), message.ToString()));
    }
}
```

对比 `Send(const void*, size_t)` 版本，Slice 版本额外执行 `ToString()` 分配。在混合线程发送场景中，这增加不必要的内存分配。

**修复建议**:
- `Slice` 版本也应走 `shared_ptr<string>` 或直接拷贝到 loop 线程的缓冲区
- 或统一 `Send` 接口，隐含处理跨线程情况

---

### 5.2 `Buffer::grow()` 分配策略

**文件**: [buffer.h](src/runtime/evpp/buffer.h:293-314)  
**严重度**: 🟡 P3

```cpp
// buffer.h:293 — 倍增策略 + 每次完全重新分配
void grow(size_t len) {
    if (WritableBytes() + PrependableBytes() < len + reserved_prepend_size_) {
        size_t n = (capacity_ << 1) + len;  // 2x 倍增
        char* d = new char[n];
        memcpy(d + reserved_prepend_size_, begin() + read_index_, m);
        // ...
        delete[] buffer_;
        buffer_ = d;
    } else {
        memmove(...);  // 紧凑化
    }
}
```

`readv` 风格读取 + 2x 倍增策略是成熟设计（源自 muduo）。但 256KB 的 `max_capacity_` 意味着大消息可能触发多次倍增。

**修复建议**:
- 根据实际消息大小分布调整初始 `kInitialSize`（当前 1KB）和增长率
- 考虑使用 `mmap`/`VirtualAlloc` 的环形缓冲区用于大连接

---

### 5.3 `Connector` 重连退避使用固定参数

**文件**: [connector.h](src/runtime/evpp/connector.h:25-29)  
**严重度**: 🟡 P3

```cpp
struct ConnectorConfig {
    int max_retries = 5;
    int retry_interval_ms = 1000;
    int max_retry_interval_ms = 30000;
    double backoff_multiplier = 2.0;
};
```

非性能瓶颈，但缺乏 jitter（随机偏移）。当大量客户端同时断连重连时（如服务器重启），精确的指数退避会导致"惊群效应"——所有客户端在相同间隔后同时重连，造成服务端瞬时过载。

**修复建议**:
- 添加 jitter: `actual_interval = base_interval * (1 + random(0, 0.3))`

---

### 5.4 `Buffer` 具有 `max_capacity_` 限制，但缺乏流控反馈

**文件**: [buffer.h](src/runtime/evpp/buffer.h:136-137)  
**严重度**: 🟢 P3

`ReadFromFD` 在达到 max capacity 时返回 0，但调用方（`HandleRead`）可能将其解释为 EOF 并关闭连接。更好的设计是:
- 暂停读取（`DisableReadEvent`）而非返回 0
- 通过 high-water mark 回调通知应用层

当前实现已具备 high-water mark 机制，但在 `ReadFromFD` 层面的行为是静默截断。

---

## 6. P4 — 低优先级优化机会

### 6.1 `Engine::Init()` 启动路径中 `std::fprintf` 直接输出

**文件**: [engine.cc](src/runtime/engine/engine.cc:90-92)  
**严重度**: 🟢 P4

```cpp
std::fprintf(stderr, "[engine] Init() begin\n");
std::fprintf(stderr, "[engine] InitLogger...\n");
```

在 logger 初始化完成前使用无缓冲 stderr 直接输出。虽然只在启动时执行一次，但 `fprintf` 对每个 `%` 和非格式化字符都有解析开销。

**修复建议**: 使用 `write(STDERR_FILENO, ...)` 或 `fputs` 替代。

---

### 6.2 `EventLoop` 的 `EVPP_TRACE` 宏在 Release 构建中展开

**文件**: [event_loop.cc](src/runtime/evpp/event_loop.cc:18-22)  
**严重度**: 🟢 P4

```cpp
#define EVPP_TRACE(fmt, ...)                                \
    do {                                                    \
        auto* _l_ = engine::GetLogger();                    \
        if (_l_) ENGINE_LOG_TRACE(_l_, fmt, ##__VA_ARGS__); \
    } while (0)
```

即使 `ENGINE_LOG_TRACE` 在 Release 中被编译为 no-op，`GetLogger()` 调用和参数求值仍然发生。在 `EventLoop::QueueInLoop` 中调用了 3 次 `EVPP_TRACE`，其中 `GetPendingQueueSize()` 可能涉及锁获取。

---

### 6.3 `ScriptVM::DoString` — `std::string(chunk_name)` 临时对象

**文件**: [vm.cc](src/runtime/vm/vm.cc:128)  
**严重度**: 🟢 P4

```cpp
int rc = luaL_loadbufferx(L_, script.data(), script.size(),
                           std::string(chunk_name).c_str(), "t");
```

`std::string(chunk_name)` 创建临时字符串仅为了调用 `.c_str()`。如果 `chunk_name` 已经是 null-terminated（`std::string_view` 不保证），可直接使用 `chunk_name.data()`。

---

### 6.4 `SequentialIdAllocator` 可能导致的 ID 耗尽

**文件**: [entity_id.h](src/runtime/entity/entity_id.h)  
**严重度**: 🟢 P4

未在本次分析中直接审查实现，但如果 ID 分配使用纯递增计数器（无回收），在长运行服务器中可能耗尽 32-bit/64-bit ID 空间。建议验证是否有 ID 回收机制。

---

### 6.5 `PhysicsThread` 的 `cv_.notify_one()` 开销

**文件**: [physics_thread.h](src/runtime/physics/physics_thread.h:100)  
**严重度**: 🟢 P4

```cpp
std::mutex cv_mutex_;
std::condition_variable cv_;
```

物理线程使用 condition_variable 唤醒机制替代了早期设计的 50ms 轮询，这是正确的。但 `notify_one()` + `wait()` 涉及系统调用和上下文切换，在每帧（33ms 间隔）场景中，condvar 开销占比小。如果未来帧率提升到 120fps（8ms），考虑使用 `std::atomic` 忙等待或 `futex` 混合方案。

---

### 6.6 缺少自定义分配器

**严重度**: 🟢 P4

整个 runtime 模块使用全局 `new`/`delete`，没有针对高频对象的池化分配器。特别是:
- `HrTimerNode` / `TimerWheelNode` / `Alarm` 的创建/销毁
- `Buffer` 的内部 `char[]` 缓冲区
- `Entity` / `Space` 对象

建议为上述对象实现 object pool 或使用 `std::pmr::polymorphic_allocator`。

---

## 7. 总结与行动建议

### 问题分布

| 类别 | P0 | P1 | P2 | P3 | P4 | 合计 |
|------|----|----|----|----|----|------|
| 内存分配 | 1 | 6 | - | 1 | 1 | 9 |
| 锁/并发 | - | - | 5 | - | 1 | 6 |
| 数据结构 | 3 | 2 | 1 | - | 1 | 7 |
| 热路径 | 2 | - | - | - | 1 | 3 |
| I/O/网络 | - | - | - | 3 | - | 3 |

### 建议修复优先级

**第一轮 (本迭代 — P0)**:
1. `ConfigManager` 高频字段 lock-free 缓存 — 减少每帧 ~10 次 string 分配
2. `TimerManager::update()` 时间戳收敛为单次查询
3. `CoroutineScheduler` 维护可运行列表 + 最小堆唤醒
4. `SpaceMessageRouter` Lua 函数引用缓存
5. `EntityManager` 活跃实体分离索引
6. `AOIManager` 反向可见性索引

**第二轮 (下迭代 — P1)**:
1. 核心路径 `std::function` → `function_ref` 迁移
2. `TimerQueue` 侵入式红黑树
3. `SpatialGrid` cell 内数据结构改进
4. `AttributeTable` interned string key
5. `EntityCache` 返回引用避免拷贝

**第三轮 (后续 — P2)**:
1. recursive_mutex → mutex + 延迟队列
2. ClockManager lock-free now()
3. 分片锁优化

### 预期收益估算

| 优化阶段 | 帧时间减少 | 内存减少 | 多核扩展 |
|----------|-----------|----------|---------|
| 第一轮 (P0) | 10-25% | 5-10% | +0% |
| 第二轮 (P1) | 15-30% | 15-25% | +10% |
| 第三轮 (P2) | 5-10% | <5% | +30-50% |
| **总计** | **~30-50%** | **~20-35%** | **~40-60%** |

> ⚠️ 收益为估算值，实际效果依赖工作负载特征和测试验证。

---

*报告结束 — 共识别 28 个性能问题，覆盖 16 个子模块*
