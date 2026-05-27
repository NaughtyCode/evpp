# CloudEngine Infrastructure Deficiency Analysis

## 概述

CloudEngine 的定位是"通用游戏服务器基础设施"——C++ 层实现基建（网络、定时器、数据库、物理），Lua 脚本层定义和执行业务逻辑。本文档从该定位出发，对工程现存的不足和缺陷进行系统性的深度分析。

**分析方法约定**：每个条目按"现状 → 根因 → 影响 → 方向"四段式展开。P0 问题（第一部分）是阻塞性缺陷——不解决则无法承载实际游戏业务。P1-P3 问题（第二至第四部分）是完善性缺陷——影响开发效率、运维质量或安全性。

**数据来源**：基于对 `src/runtime/` 下全部源代码的逐行审查，覆盖 engine、vm、script、config、database、physics、evpp 七个子系统及其绑定层，以及 `resources/script/` 下的全部 Lua 脚本。本轮新增：跨线程 RunInLoop 生命周期分析、Lua 全局命名空间审计、evpp 层线程安全检查、数据库背压路径、Engine Cleanup 顺序依赖分析。

---

## 第一部分：P0 阻塞性缺陷深度分析

P0 定义为"不解决则业务无法上线"的缺陷。以下按严重程度排序。

---

### P0-1：缺少实体模型（Entity/Actor/GameObject 抽象层）

#### 现状

当前引擎向下暴露的最上层抽象是**网络连接**（TCP Connection）和**定时器**（Timer）。开发者拿到的是：

```lua
-- 当前能做的（来自 net_tcp_server_bind.cc:491-508）
server.on_connect = function(conn)
    conn.on_message = function(conn, data)
        -- data 是原始字节串
        conn:send("reply")
    end
end
```

引擎内部最接近"实体存储"的结构是 `VMCustomPtrStore`（`src/runtime/vm/custom_ptr_store.h`），它是对 `lua_State` 内嵌 `void*` 数组的薄封装：提供 Set/Get/Push/Find/Contains 等操作，按 1-based 索引访问，支持 Reserve/Capacity 管理。但**它不提供任何语义**——不过是一个带索引的指针袋。

#### 根因分析

当前架构的分层是：

```
Lua 业务层 (手写)
  └─ net.server/client 封装 (Lua class, server.lua:240行)
       └─ C++ 绑定 (net_tcp_server_bind.cc:625行)
            └─ evpp 网络库 (TCPServer / TCPConn)
                 └─ libevent (epoll/kqueue)
```

其中存在一个**巨大的抽象断层**：从"网络连接"到"游戏实体"之间没有任何中间层。以下能力全部缺失：

| 缺失能力 | 为什么它是"基础设施"而非"业务逻辑" |
|----------|-------------------------------------|
| 实体 ID 分配 | 每个游戏服务器都需要唯一标识玩家/NPC/道具，ID 分配策略（雪花 ID、分段 ID）应该是基建 |
| 实体属性管理 | 有属性（HP/位置/等级）是实体的定义特征，读写/变更通知应该是基建 |
| 实体生命周期 | 创建/激活/休眠/销毁的状态机是所有游戏通用的，销毁时自动取消关联定时器和回调 |
| 实体间消息传递 | 实体 A 向实体 B 发消息，B 离线时缓存——这是通用模式 |
| 实体-连接绑定 | 玩家"上线 = 创建 Player Entity + 绑定 Connection"，断线 = "解绑但不销毁 Entity（等重连）" |

#### 代码证据

`net_tcp_server_bind.cc` 展示了 C++ 侧维护 Lua 对象引用的结构和生命周期管理：

```cpp
// ConnCtx（行35-41）——每个连接维护：
struct ConnCtx {
    evpp::TCPConnPtr conn;
    lua_State* L = nullptr;
    int instance_ref = LUA_NOREF;      // Lua conn 实例的注册表引用
    int server_inst_ref = LUA_NOREF;   // Lua server 实例引用（用于fallback回调）
    bool disposed = false;             // 防止 use-after-free
};
```

**定量分析——绑定样板代码的复制粘贴规模**：

| 绑定文件 | 行数 | Context 结构体 | luaL_ref/luaL_unref | disposed 引用 | __gc/__index=mt |
|---------|------|--------------|--------------------|-------------|----------------|
| net_tcp_server_bind.cc | 625 | ServerCtx + ConnCtx | 10 | 28 | 4 |
| net_tcp_client_bind.cc | 378 | ClientCtx | 4 | 17 | 5 |
| net_kcp_server_bind.cc | 441 | KcpServerCtx | 17 | 14 | 2 |
| net_kcp_client_bind.cc | ~340 | KcpClientCtx | ~10 | 14 | 2 |
| net_udp_server_bind.cc | ~420 | UdpServerCtx | 16 | 10 | 4 |
| net_udp_client_bind.cc | ~280 | UdpClientCtx | ~9 | 9 | 6 |

**合计**：~2500 行绑定代码，其中约 80% 是跨 6 个文件复制的相同模式。60 处 `luaL_ref`/`luaL_unref`，92 处 `disposed` 引用，30 处 `__gc`/`__index = mt` 注册。

这个模式每新增一种绑定类型都需要完整复制一遍。**这不是"没有做实体模型"，而是当前架构迫使每增加一个对象类型就需要手工重复此模板约 200-400 行代码。**

#### 影响量化

假设一个中等规模游戏（5000 在线玩家、10 种 NPC 类型、100 种道具），在没有实体框架的情况下：
- 每个实体类型需要手写创建/销毁/属性管理代码（~200-500 行/类型）
- 实体间引用需要手动维护弱引用（防止循环引用导致 Lua GC 不回收）
- 实体销毁时需要手动取消其所有定时器、移除所有事件监听
- 实体序列化/反序列化需要逐字段手写

**结论：没有实体框架，90% 的 Lua 业务代码将是非业务逻辑的"胶水代码"。**

**补充发现 — `class.lua` 的能力与局限**：

`class.lua`（139 行）提供单继承 + 弱引用热重载 + `isinstanceof`。但其设计有几个限制：

1. **`isinstanceof` 是 O(depth)**：沿 `__index` 链逐级遍历查找目标类，每调用一次走完整条链。对于深继承层级 + 高调用频率的场景（如每帧检查碰撞回调中的实体类型），累积开销不可忽视
2. **弱值注册表的隐含假设**：`_registry` 使用 `__mode = "v"`（弱值），假设类表在没有实例引用时可以被 GC。但热重载时调用 `Class("Name")` 复用旧表——如果旧表已被 GC，则创建一个全新的类表，**旧实例的 `__index` 仍指向已被 GC 的旧 metatable**，导致方法查找静默失败
3. **与 `ScriptImporter::ClearCache` 的交互未定义**：`ClearCache` 清空 `package.loaded`，但 `class.lua` 的 `_registry` 保留旧类表。重新 import 后，新脚本定义的方法覆盖旧类表（热重载预期行为），但旧脚本注册的全局回调（如 `server.on_connect`）是否更新取决于脚本结构

#### 改进方向

建议在 C++ 层引入 `Entity` 基类，至少提供：
1. `entity_id` 分配（64-bit，支持雪花 ID 或分段分配）
2. 属性表（key-value，支持 int/float/string/bool，支持变更回调）
3. 生命周期状态机：`Created → Active → Suspended → Destroyed`
4. 自动资源管理：Entity 销毁时自动取消其所有定时器、解除所有事件监听
5. Entity-Component 模型：属性集成为 Component，实体可以动态装配 Component
6. 与连接的绑定/解绑：`bind_connection(conn)` / `unbind_connection()`

---

### P0-2：缺少消息分帧协议

#### 现状

消息从网络到达 Lua 的完整路径如下：

```
网络数据到达 → libevent 触发读事件
  → TCPConn::HandleRead() [tcp_conn.cc:176]
    → input_buffer_.ReadFromFD(fd, &serrno)  // 读入原始字节
    → msg_fn_(shared_from_this(), &input_buffer_)  // 触发回调
      → net_tcp_server_bind.cc:493 lambda
        → std::string data = buf->NextAllString()  // ★ 关键行 499
        → CallInstMethodStr(L, conn_ref, "on_message", data)  // 传给 Lua
```

**`buf->NextAllString()`** 读取缓冲区中的全部数据，不考虑消息边界。该调用覆盖了所有需要消息分帧的路径：

| 位置 | 协议 | 说明 |
|------|------|------|
| net_tcp_server_bind.cc:499 | TCP Server | 服务端消息回调 |
| net_tcp_client_bind.cc:173 | TCP Client | 客户端消息回调 |
| tcp_conn.cc:108 | TCP Conn | SendStringInLoop |

`Buffer::NextAllString()` 返回 `[read_index_, write_index_)` 之间的全部数据，然后 Reset 缓冲区。这意味着：
- TCP 粘包：Lua 收到的是多个消息拼接的一个字符串
- TCP 拆包：Lua 收到的是消息的片段
- 实际网络（非 localhost）：两种现象混合出现

#### 为什么现有测试没有暴露问题

`test_net_client_server.lua` 中的所有测试都跑在 localhost 且每次只发一条消息等待回复。这些测试在真实的广域网延迟和 MTU 限制下**必然失败**。

#### 为什么这是 P0

游戏协议本质上是"在网络连接上传输结构化消息"。如果没有消息分帧：
1. **每个业务都需要自行实现分帧**——这应该是一次做好、所有人复用的基础设施
2. **Lua 侧做分帧效率极低**——Lua 字符串操作比 C++ 慢 10-100 倍
3. **不同模块的分帧实现不兼容**——混乱且 bug 多

#### Buffer 已有的基础与待修复问题

`Buffer` 类已经支持 `AppendInt16/AppendInt32`（网络字节序）和 `PrependInt16/PrependInt32`。但有两个待修复的底层问题：

- `buffer.h:121`：`Reserve()` 方法标记 "TODO add the implementation logic here" —— **空实现**
- `buffer.h:141`：标记 "TODO XXX Little-Endian/Big-Endian problem" —— **字节序问题未解决**

#### 改进方向

在 C++ 侧实现 `LengthPrefixedCodec`，对 Lua 侧完全透明：

```
[4字节长度] [消息体]
```

解码逻辑：循环检查长度头 → 提取完整消息 → 分派到 Lua → 残留数据留待下次。将 Codec 集成到所有协议绑定（TCP/KCP/UDP）的消息回调链路中。

---

### P0-3：测试基础设施严重不足

#### 现状

**测试文件清单**（`src/tests/unit/`）：全部是 evpp 层（buffer、event_loop、tcp_client/server、http_client/server、udp_server、invoke_timer、sockets、dns_resolver 等）。

**完全无测试的模块**：

| 模块 | 代码量 | 风险等级 | 无测试的后果 |
|------|--------|---------|-------------|
| `engine/engine.cc` | 457 行 | 极高 | Init/Start/Cleanup 顺序错误无法自动检测 |
| `vm/vm.cc` | 426 行 | 极高 | DoString/DoFile/UpdateScript 边界条件无覆盖 |
| `vm/script_importer.cc` | 260 行 | 高 | 循环依赖、搜索路径等边界无验证 |
| `config/config.cc` | 338 行 | 高 | Reload 不通知子系统的问题不会被发现 |
| `timer/timer_manager.h` | ~350 行 | 高 | 定时器泄漏、重复触发无法检测 |
| `script/*.cc` | 13 个文件 | 高 | luaL_ref 泄漏、disposed 标记遗漏无法检测 |
| `database/data_service/` | 6 个文件 | 极高 | 并发 SPSC 队列正确性 |
| `physics/` | 4 个文件 | 中 | 跨线程命令队列正确性 |
| `database/mongo_bind/` | 46 个文件 | 中 | MongoDB 绑定正确性 |

**Lua 侧测试**（仅 2 个文件）：`test_net_client_server.lua`（458 行）、`msgpack_test.lua`。无 timer/log/class.lua/net 封装/import 系统测试。

**engine 层测试文件数：0**。

#### 为什么测试缺失是 P0

对于"基础设施建设"，**没有测试意味着没有可靠性承诺**。当前只有"作者自己跑过"级别的验证。任何严肃的使用者都需要确信定时器不会悄悄泄漏、网络绑定不会在特定边界条件下 crash、数据库请求在并发场景下不会丢。

#### 根因

1. **Engine 单例使测试难以隔离**：`Engine::Instance()` 是全局状态
2. **缺少 mock 基础设施**：没有 mock EventLoop、mock TimerManager
3. **C++ 绑定层和 Lua 状态耦合**：测试 ScriptVM 需要完整的 Lua 环境
4. **没有测试优先的设计习惯**：76 处 `fprintf(stderr, ...)` 暗示"加 print → 手动跑 → 看输出"的开发流程

#### 改进方向

1. 解耦单例（为测试铺路）
2. ScriptVM 单元测试：创建 VM → 执行 Lua → 验证结果
3. TimerManager 单元测试
4. Lua 模块测试框架
5. 集成测试

---

### P0-4：单 Lua VM 架构瓶颈 — 多 VM 推广缺失

#### 现状

`Engine::Init()`（`engine.cc:151`）中仅创建一个业务 VM：

```cpp
script_vm_ = std::make_unique<ScriptVM>();  // 唯一一个业务 VM
```

所有业务逻辑在这个 VM 中串行执行。单线程单 VM，无任何并行能力。

#### 但工程中已经有三类 VM 实例

| VM 类型 | 位置 | 线程 | 数量 |
|---------|------|------|------|
| `ScriptVM` | `Engine::script_vm_` | 主事件循环线程 | 1 |
| `PhysicsScriptVM` | `PhysicsSystem` 内 | 物理线程 | 1 |
| `DBScriptVM` | 每个 `DBThread` 内 | DB 工作线程 | N (默认4) |

`PhysicsScriptVM` 继承 `ScriptVM`，通过 `VMCustomPtrStore` 注册物理子系统对象指针，运行在独立物理线程上。`DBScriptVM` 继承 `ScriptVM`，每个 DBThread 持有一个。**多 VM 架构的技术基础已经存在**——`ScriptVM` 的移动语义（`vm.h:40-41`）、`VMCustomPtrStore` 的对象注册、`moodycamel::ConcurrentQueue` 的跨线程通信——这些零件都齐了。缺失的是**将这一模式推广到业务 VM 层的架构决策。**

#### 根因

当前架构中，业务 VM 的外部依赖通过**全局单例**隐式耦合：

```
Engine::Instance()            — 30+ 次调用
ConfigManager::Instance()     — 10+ 次调用
TimerManager::instance()      — engine.cc, timer_bind.cc
DatabaseService::Instance()   — engine.cc, db_service_main_bind.cc
PhysicsSystem::Instance()     — physics_bindings.cc
MongoSystem::Instance()       — engine.cc, bind_misc.cc
PhysicsEngineBridge::Instance() — engine.cc
```

**7 个单例类**，在 engine 代码中有 **80+ 次 `.Instance()` 调用**。如果要让多个业务 VM 独立运行，核心问题不是"创建多个 lua_State"，而是**依赖注入**。

#### 影响

1. **CPU 利用率上限 = 1 核**
2. **业务隔离性差**（一个 Room 脚本错误影响其他 Room）
3. **无法热更新部分模块**
4. **单点阻塞**（任何一个耗时操作拖慢所有业务逻辑）

在 2026 年，服务器 CPU 通常是 64-128 核，单核架构不可接受。

#### 改进方向

推荐 Space/Room-per-VM 模式，复用已有 DBScriptVM 的线程所有权模型作为参考模板。关键技术点：连接分发、VM 间消息队列、定时器隔离、依赖注入。

---

### P0-5：luaL_error 异常安全问题 — C++ 析构函数被绕过

#### 现状

`luaL_error` 使用 `longjmp` 实现，会**跳过栈上所有 C++ 对象的析构函数**。在绑定代码中，大量使用 `luaL_error` 进行输入校验，而此时栈上存在 `std::unique_ptr`、`std::string` 等 RAII 对象：

```cpp
// net_tcp_server_bind.cc:354-537 (l_net_server_listen)
int l_net_server_listen(lua_State* L) {
    auto* ctx = new ServerCtx();                   // 裸 new — 无 RAII 保护
    auto name = std::string("lua_server_") + ...;   // std::string 在栈上
    ctx->server = std::make_unique<evpp::TCPServer>(...);
    if (!ctx->server->Init()) {
        // ...手动清理...
        return luaL_error(L, "server init failed");  // longjmp!
    }
}
```

该问题存在于所有 6 个协议绑定文件中（tcp_server、tcp_client、kcp_server、kcp_client、udp_server、udp_client）。

#### 改进方向

1. 在使用 `luaL_error` 的函数中，将 C++ RAII 对象分配在堆上而非栈上
2. 使用 `lua_pushstring(L, errmsg); return lua_error(L);` 替代直接 `luaL_error`
3. 长远方案：使用 Lua 5.5 的 `lua_resetthread` + `lua_yield` 替代 longjmp

---

### P0-6：无消息/负载大小限制 — 内存耗尽 DoS 向量

#### 现状

所有 C++ 绑定中对 `luaL_checklstring` 返回的数据长度**不做上限检查**：

```cpp
// net_tcp_server_bind.cc:132 — conn:send(data)
size_t len = 0;
const char* data = luaL_checklstring(L, 2, &len);
ctx->conn->Send(data, len);  // len 无上限

// net_tcp_client_bind.cc:191 — client:send(data)  
size_t len = 0;
const char* data = luaL_checklstring(L, 2, &len);
conn->Send(data, len);  // len 无上限
```

同样的问题存在于 UDP 绑定、KCP 绑定、HTTP POST body 等所有数据入口。同时网络侧 `Buffer` 也没有总容量上限——`ReadFromFD` 会持续追加数据，恶意或异常的客户端可以通过持续发送数据耗尽服务器内存。

#### 影响

一个简单的 `while true do conn:send(string.rep("A", 1024*1024*1024)) end` 可以分配不受控的内存。在没有消息分帧的情况下，攻击面更大——攻击者可以在单个 TCP segment 中发送远超预期大小的数据。

#### 改进方向

1. 在所有 `luaL_checklstring` 调用后添加最大长度检查（默认 64KB 或可配置）
2. Buffer 添加 `max_capacity` 限制，超限时断开连接
3. 这些限制应可配置，不同业务场景有不同的需求

---

### P0-7：跨线程 RunInLoop 生命周期竞态条件 — lua_State 悬空指针

#### 现状

KCP 和 UDP 绑定层中，网络消息通过 `RunInLoop` 从接收线程派发到主线程的 Lua 回调。消息处理器捕获了**原始 `lua_State*` 指针**：

```cpp
// net_kcp_server_bind.cc:58-91 — BindKcpMessageHandler
ctx->server->SetMessageHandler([ctx, main_loop](evpp::EventLoop*, evpp::kcp::MessagePtr& msg) {
    lua_State* L_ptr = ctx->L;  // 捕获原始指针
    // ...
    main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip, conv]() {
        if (!L_ptr || msg_ref == LUA_NOREF) return;  // 空指针检查不足以防护
        lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);  // L_ptr 可能已悬空
    });
});
```

同样的模式出现在 `net_udp_server_bind.cc:79` 和 `net_kcp_server_bind.cc:73`。

#### 根因

1. **销毁顺序不可控**：`ShutdownKcpServerBindings` / `ShutdownUdpServerBindings` 在 `Engine::Cleanup` 中被调用，但此时 `script_vm_` 尚未销毁。如果在 Shutdown 和 VM 销毁之间仍有排队的 `RunInLoop` 回调执行，则 `L_ptr` 有效。**但这个顺序是隐式的，没有显式的 happens-before 保证。**

2. **`g_net_alive` 模式未推广**：HTTP 绑定（`net_http_bind.cc`）使用 `g_net_alive` atomic + mutex 保护 pending ref 列表 + TOCTOU 安全的 Shutdown 交互，是已证明的解决方案。但 KCP 和 UDP 绑定没有采用这个模式。

3. **RunInLoop 延迟 delete 模式的固有问题**：绑定层普遍使用 `loop->RunInLoop([del_ctx] { delete del_ctx; })` 作为安全的释放方式。但这假设 loop 在 delete 发生时仍在运行——在 Shutdown 期间这个假设可能不成立。

#### 影响

- **UAF (Use-After-Free) 风险**：如果 Lua state 在 `RunInLoop` 回调执行前被销毁，`lua_rawgeti` 操作已释放的内存
- **非确定性崩溃**：依赖于 Shutdown 时序（GC 触发时机、event loop 排空速度、线程调度）
- 在 Release 构建中可能表现为低概率的 SIGSEGV，极难复现和调试

#### 代码证据

跨 4 个绑定文件的 20+ 处 `RunInLoop` 调用，全部捕获原始 `lua_State*` 或原始 `KcpServerCtx*`：

| 文件 | RunInLoop 调用 | 捕获的原始指针 |
|------|---------------|---------------|
| net_kcp_server_bind.cc | 73, 112, 280, 409 | L_ptr, ctx, L, old_msg_ref, old_inst_ref |
| net_udp_server_bind.cc | 79, 124, 298, 381 | L_ptr, ctx, L, old_msg_ref, old_inst_ref |
| net_tcp_server_bind.cc | 183, 246, 289, 356, 491 | del_ctx, ctx |
| net_tcp_client_bind.cc | 171, 244, 294 | del_ctx, ctx_ptr |

#### 改进方向

1. 将 HTTP 绑定的 `g_net_alive` + mutex + pending ref 模式推广到 KCP 和 UDP 绑定
2. 在 `RunInLoop` 回调中增加 `g_net_alive.load()` 检查
3. Shutdown 时先设置 `g_net_alive = false`，然后排空 event loop（确保所有已排队的回调执行完毕），最后释放资源
4. 长期方案：使用 `std::weak_ptr` + `std::shared_ptr<LuaStateGuard>` 而非原始 `lua_State*`

---

## 第二部分：架构层面的核心缺失（P1-P2）

### 2.1 子系统设计质量极端不均衡

#### 现状

代码库中存在三个明显不同的设计质量层级：

**A 级 — 设计典范**：

| 子系统 | 亮点 |
|--------|------|
| PhysicsEngineBridge | 逐方法线程验证（VerifyMainThread）、编译期访问控制（PHYSICS_INTERNAL_ACCESS）、SPSC 队列隔离、逐方法文档化注释 |
| DBThread | 线程所有权标注（[MT]/[DBT]/[SPSC]/[ATOM]）、清晰的 EventLoop 契约、per-thread logger 隔离 |
| HTTP 绑定 | g_net_alive atomic 关闭标志、mutex 保护的 pending ref 追踪、TOCTOU 安全的 Shutdown 交互 |

**B 级 — 功能正确但缺乏抽象**：

| 子系统 | 问题 |
|--------|------|
| MongoDB 绑定（46 个文件） | 使用 `GetUserdata<T>`/`NewUserdata<T>` 模板辅助，`__gc` 正确清理，但每个类型仍需手写大量重复代码 |
| ScriptVM | 功能完备，但 `luaL_openlibs` 加载全部标准库无沙箱 |

**C 级 — 手工样板代码堆积**：

| 子系统 | 问题 |
|--------|------|
| 网络绑定（6 个协议，13 个文件） | 80% 重复样板代码，手动 lightuserdata + disposed 标记，无模板辅助函数 |

#### 根因

这不只是"有的模块写得好有的写得差"。**核心区别在于是否使用了封装抽象**：

- Physics/Bridge 使用 `PHYSICS_INTERNAL_ACCESS` 宏在编译期切断外部直接访问
- MongoDB 绑定使用 `GetUserdata<T>` / `NewUserdata<T>` 模板统一管理生命周期
- 网络绑定每个协议手动实现完全相同的 lightuserdata + disposed + luaL_ref + __gc 模式

MongoDB 绑定虽然也是 46 个文件的大规模手工代码，但至少使用了 `GetUserdata<T>` / `NewUserdata<T>` 模板（定义在 `bind_util.h`），统一了用户数据生命周期管理。网络绑定没有做这件事。

#### 改进方向

1. 将 MongoDB 绑定层的 `GetUserdata<T>` / `NewUserdata<T>` 模式推广到网络绑定层
2. 建立跨子系统的设计标准文档，引用 PhysicsEngineBridge 和 DBThread 为参考实现
3. 代码审查中明确检查是否使用了已建立的抽象模式

---

### 2.2 绑定代码样板爆炸

已在 P0-1 中量化。新增结论：**MongoDB 绑定层（46 个文件）虽然也包含重复代码，但使用了 `bind_util.h` 中的 `GetUserdata<T>`/`NewUserdata<T>` 模板统一了生命周期管理，设计质量明显高于网络绑定层。网络绑定层应该采用同样的方式统一 lightuserdata 管理。**

---

### 2.3 缺少空间索引 / AOI 系统

**现状**：引擎无空间划分、网格、四叉树或场景图实现。`PhysicsEngineBridge` 虽然集成了 Jolt Physics，但其结果（transforms、diff_packets）未被接入游戏对象层（`engine.cc:434-436` 中明确注释了"would go here"）。

---

### 2.4 缺少服务间 RPC 框架

**现状**：TCP/UDP/HTTP/KCP 仅提供原始字节传输，无序列化协议层、无 IDL、无 RPC 桩代码生成。

---

### 2.5 缺少热更新系统

`class.lua` 有弱引用注册表支持类热重载，`ScriptImporter::ClearCache()` 可清除 require 缓存。但无文件监控、变更检测、验证/回滚。`ConfigManager::Reload()` 有完整的重载逻辑但**没有任何子系统收到重载通知**。

---

### 2.6 生命周期模型过于简化

`Init → Start → Run → Shutdown` 线性流程，对应 Lua 的 `InitScript → UpdateScript → DestroyScript`。无场景切换、加载过渡、分阶段初始化、优雅降级。状态机仅基于 `running_` 和 `cleaned_up_` 两个 flag。

---

### 2.7 Lua 全局命名空间污染 — 无模块隔离机制

#### 现状

`ExportAll`（`script_bind.cc:19-55`）将所有 C++ 模块导出到 Lua 全局表：

```cpp
ExportLog(vm);       // → 全局: log_info, log_error, log_warn, log_debug
ExportTimer(vm);     // → 全局: timer.interval, timer.timeout, timer.cancel
ExportNet(vm);       // → 全局: net.server.listen, net.client.connect, ...
ExportMsgPack(vm);   // → 全局: cmsgpack, cmsgpack_safe
ExportImport(vm);    // → 全局: import, set_import_paths, ...
ExportMongo(vm);     // → 全局: mongo.* (46 个绑定文件的导出)
ExportDbService(vm); // → 全局: db_is_running, db_is_healthy, db_send_request, ...
```

所有业务脚本共享同一个全局命名空间。`ScriptImporter` 使用 `package.loaded` 缓存模块以避免重复加载，但**不追踪每个模块拥有哪些全局变量**。

#### 根因

Lua 默认是单命名空间语言。模块隔离需要显式的设计决策（如每个模块返回一个局部表而非设置全局变量）。当前的架构选择是"方便优先"——将所有 API 暴露为全局，这样脚本编写时无需 `local net = require("net")`。

#### 影响

1. **模块间意外冲突**：两个独立开发的模块定义了同名全局函数，后加载的静默覆盖先加载的
2. **热重载不完全**：`ScriptImporter::ClearCache()` 清空 `package.loaded` 但不清除全局变量。重载一个模块后，其旧版本设置的全局变量仍存在
3. **测试隔离不可能**：无法在同一进程中运行多个测试——它们会互相覆盖全局状态
4. **`class.lua` 的弱引用注册表与清理不一致**：类注册表（`_registry`）使用 `__mode = "v"`（弱值），但类表本身是强引用。ClearCache 后旧类方法仍存活

#### 改进方向

1. 将 API 导出从全局表迁移到模块返回值（`local net = import("runtime.net")`）
2. 跟踪每个模块的全局变量所有权，使 `ClearCache` 能正确清理
3. 短期方案：在 `ClearCache` 中增加全局变量清理步骤

---

### 2.8 数据库请求无背压通知 — 队列满时静默丢弃

#### 现状

`DatabaseService::SendRequest` 返回 `bool`，但调用方通常不检查返回值：

```cpp
// db_service_main_bind.cc:205-206
bool ok = DatabaseService::Instance().SendRequest(std::move(req));
lua_pushboolean(L, ok ? 1 : 0);  // 返回给 Lua —— 但 Lua 侧未必检查
return 1;
```

`DBThread::EnqueueRequest` 内部使用 `moodycamel::ConcurrentQueue::enqueue`，队列满时直接返回 `false`。请求被静默丢弃——无日志、无重试、无超时通知。

#### 根因

SPSC 队列的固定容量（默认为 moodycamel 的默认值 128）不被视为"需要监控的资源"。轮询分发（round-robin）虽然均匀，但无法处理热点——某个 DBThread 可能比其他更忙。

#### 影响

1. **静默数据丢失**：Lua 侧调用 `db_send_request` 返回 `true`（实际 enqueue 成功）的时刻和返回 `false` 的时刻之间没有显式的"你的请求被丢弃了"通知
2. **调试极其困难**：数据不一致时，无法区分"请求未发出""请求发出但执行失败""请求被队列丢弃"三种情况
3. **无降级路径**：队列满时没有退避、重试、或降级写入的机制

#### 改进方向

1. 队列满时记录 WARN 日志
2. 在 DbResponse 中增加 `kDropped` 状态
3. Lua 侧 `db_send_request` 应在返回 false 时让调用方感知
4. 增加 per-DBThread 的 pending 计数和背压指标

---

### 2.9 线程安全检查仅在 Debug 构建中生效 — Release 零防护

#### 现状

`evpp/inner_pre.cc` 中实现了 `event_add`/`event_del` 的线程安全检查——但**仅在 `H_DEBUG_MODE` 宏下编译**：

```cpp
// inner_pre.cc:35-56
#ifdef H_DEBUG_MODE
static std::map<struct event*, std::thread::id> evmap;
static std::mutex mutex;

int EventAdd(struct event* ev, const struct timeval* timeout) {
    {
        std::lock_guard<std::mutex> guard(mutex);
        // —— 检查重复 add、跨线程 del ——
        assert(false && "event_add twice");
    }
    // ...
}
#endif
```

在 Release 构建中，`EventAdd` 和 `EventDel` 直接透传到底层 `event_add`/`event_del`，无任何线程检查。

#### 根因

libevent 要求 `event_add`/`event_del` 必须在拥有该 event 的 event loop 线程上调用。这是一个非线程安全的 C API。Debug 构建的检查器（`evmap` map）证明了开发团队**意识到了风险**，但没有在 Release 中保留最基本的检查。

#### 影响

Release 构建中，从错误线程调用 `event_add`/`event_del` 不会触发任何诊断——直接导致 libevent 内部数据结构损坏。表现为：
- 非常低概率的崩溃
- 事件丢失（定时器不触发、连接不收数据）
- 调试极其困难（因为 Debug 构建不会触发）

#### 代码证据

`inner_pre.cc:31-33`：`evmap` 和 `mutex` 定义在 `#ifdef H_DEBUG_MODE` 内。整个文件只有 100 行——这说明增加 Release 保护非常容易，但未做。

---

## 第三部分：系统级缺陷（P2-P3）

### 3.1 网络层

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| 无消息分帧 | P0 | NextAllString() 导致 TCP 粘包/拆包 |
| 无消息大小上限 | P0 | 无限制的 send/recv → 内存耗尽 DoS |
| 缺少消息优先级 | P2 | FIFO 发送，无优先级队列 |
| 缺少带宽管理 | P2 | 无每连接发送限速，无全局带宽上限 |
| 缺少连接级加密 | P2 | 仅 HTTP 有 SSL 支持 |
| 无连接数上限 | P2 | 恶意客户端可耗尽文件描述符 |
| HTTP 优雅关闭未实现 | P3 | `http_server.cc:294,307` 标注 TODO |
| Buffer 字节序问题未修 | P2 | `buffer.h:141` 标注 TODO |
| Buffer::Reserve 空实现 | P2 | `buffer.h:121` 标注 TODO — 空方法体 |
| Connector 重试未实现 | P3 | `connector.cc:132` 标注 TODO |
| DNS 仅 IPv4 | P2 | `dns_resolver.h:14` 标注 TODO |
| DNS `new shared_ptr` 泄漏 | P2 | `dns_resolver.cc:190` — heap-allocate shared_ptr 透传给 C 回调，若回调从不触发则永久泄漏 |
| send() 未检查返回值 | P3 | `tcp_conn.cc` 和 bind 文件中的 `conn->Send()` 忽略返回值 |

### 3.2 脚本系统

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **无沙箱** | **P1** | `vm.cc:23` — `luaL_openlibs(L_)` 加载全部标准库（含 `os.execute`、`io.open`、`debug`） |
| 无协程集成 | P1 | Lua 5.5 内置协程但引擎提供零调度支持 |
| 无错误恢复 | P2 | `UpdateScript` 出错仅日志 + pop（`vm.cc:73-85`），VM 栈可能不一致 |
| luaL_error 异常不安全 | P1 | longjmp 跳过 C++ 析构函数（详见 P0-5） |
| 全局钩子脆弱 | P2 | `InitScript`/`UpdateScript`/`DestroyScript` 是全局函数 |
| 无 Lua 性能分析 | P2 | Perfetto trace 仅在 C++ 层 |
| import 无循环依赖检测 | P2 | `ScriptImporter::ImportSingle` 仅在加载后检查缓存 |
| 日志无 Lua 源位置 | P3 | `log_bind.cc` 硬编码 `"[lua] {}"` 前缀 |
| 错误日志限流仅 UpdateScript | P3 | DoString/DoFile 的错误日志无限流 |
| **lua_gc 仅查询不控制** | P3 | `engine.cc:367` 和 `vm.cc:30` 仅查询内存使用，无 `lua_gc(L, LUA_GCCOLLECT)` 主动 GC，依赖 Lua 自动 GC 节奏 |
| **Lua 错误派发不一致** | P2 | HTTP 用 pcall 安全回调；TCP 用 CallInstMethodStr 包装；KCP 用直接 lua_pcall；Timer 用 CallLuaFunction 单日志。四种模式，无统一错误传播策略 |
| **msgpack 编码无大小上限** | P2 | `l_msgpack_pack` 可编码任意深度和任意大小的 Lua 表，无 payload 限制 |
| **RunInLoop 延迟 delete 竞争** | P2 | 绑定层普遍用 `RunInLoop([del_ctx]{delete del_ctx;})` 延迟释放 —— 假设 loop 仍在运行。Shutdown 期间 loop 停止后此假设不成立 |
| **全局变量导出无追踪** | P2 | `ExportAll` 将数十个函数注册到 Lua 全局表。ClearCache 不清除全局变量，热更后残留旧状态 |

### 3.3 数据库层

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| 仅 MongoDB | P2 | 无抽象接口支持多后端 |
| 无 ORM | P2 | 手动构造 BSON 文档 |
| 无缓存层 | P2 | 每次请求都走 SPSC → DBThread → MongoDB 网络 IO |
| cursor 泄漏风险 | P2 | `db_thread.cc:589,612,828` — catch(...) 中 cursor->Destroy() 后 rethrow |
| ParseOperationName 缓冲区固定 32 字节 | P3 | `db_service_main_bind.cc:24` — 超长输入静默截断，可能生成意外的操作匹配 |
| 请求队列满时静默丢弃 | P1 | `EnqueueRequest` 返回 false 时无日志、无重试、无降级通知 |

### 3.4 配置系统

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **Reload 无通知** | **P1** | `ConfigManager::Reload()` 原子替换配置但零回调机制 — 日志级别、定时器间隔、DB 连接池大小等实际上不可运行时调整 |
| 无 Schema 校验 | P2 | glaze 仅检查 JSON 格式，不校验字段范围和必填 |
| Release 用 Public 配置 | P2 | `engine.cc:127-131` — `#ifndef NDEBUG` 用 Dev，否则用 Public，无显式环境选择 |

### 3.5 物理子系统

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **结果未接入** | **P1** | `engine.cc:432-436` 中 FetchResult 结果被注释掉（"would go here"），物理计算完全丢弃 |
| 场景路径硬编码 | P2 | `scene.json` 写死在 `engine.cc:159` |
| FetchResult 超时硬编码 | P2 | timeout=5ms，超时后静默跳过 |

### 3.6 定时器系统

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| 层级过重 | P2 | 三种定时器类型 + 五种时钟源，服务器侧只需 ms 级统一 API |
| 无 Lua 生命周期绑定 | P1 | 实体销毁时需手动取消关联定时器 |
| TimerManager 是单例 | P2 | 无法 per-VM 隔离 |
| DumpStats 使用 std::cout | P3 | `timer_manager.cc:664-682` — 第三诊断通道，绕过日志系统 |

---

## 第四部分：工程化缺陷（P2-P3）

### 4.1 三条独立的诊断输出通道

工程中同时存在三种互不协调的诊断输出：

| 通道 | 位置 | 数量 | 问题 |
|------|------|------|------|
| Quill 日志 | `ENGINE_LOG_*` 宏 | 全工程 | 正确的日志系统 — 时间戳、级别、轮转 |
| fprintf(stderr) | 13 个文件 | 76 处 | 绕过 Quill — 无时间戳、无级别 |
| std::cout | `timer_manager.cc:664-682` | ~15 处 | 再一个通道 — 仅 TimerManager DumpStats |

三个通道的输出在终端交错，无统一格式，无统一的级别控制。`fprintf` 输出在 stderr，`std::cout` 输出在 stdout，Quill 日志输出到文件——三位一体的诊断信息被分散在三个独立的目的地，排查问题时需要同时查看三个来源。

### 4.2 硬崩溃路径 — 4 个 abort()

| 位置 | 触发条件 | 严重度 |
|------|---------|--------|
| `vm.cc:20` | `luaL_newstate()` 返回 nullptr | P2 |
| `engine.cc:54` | `GetScriptVM()` 在 Init() 前调用 | P2 |
| `event_loop.cc:38` | `event_base_new()` 失败 | P2 |
| `event_loop.cc:191` | `event_base_dispatch()` 重复调用 | P2 |

对于服务器基础设施，任何 `abort()` 都意味着整个进程不可恢复地终止，所有在线玩家状态丢失。

### 4.3 未完成的 TODO 项

**18 个 TODO/FIXME/HACK/XXX** 标记，其中部分直接影响正确性：

| 位置 | 内容 | 严重度 |
|------|------|--------|
| `buffer.h:121` | Reserve 方法空实现 | P2 |
| `buffer.h:141` | 字节序问题 | P2 |
| `http_server.cc:294,307` | 优雅关闭未实现 | P2 |
| `connector.cc:132` | 重连逻辑未实现 | P2 |
| `dns_resolver.h:14` | IPv6 DNS 未实现 | P2 |
| `dns_resolver.cc:217` | dns_req_ 可能泄漏 | P2 |
| `event_loop.cc:301` | 测试代码缺失 | P3 |
| `udp_server.cc:221` | recvmmsg 性能优化 | P3 |

### 4.4 单例滥用

**7 个单例类**，**80+ 次 `.Instance()` 调用**。后果：无法同进程多 Engine 实例、无法隔离 Space/Room 配置、所有绑定硬依赖全局 Engine EventLoop。

### 4.5 编译器警告配置不一致

- **UNIX**：`-Wall -Wextra -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings -Wsign-compare -Wfloat-equal`
- **MSVC**：11 个 `/wd` flag 禁用几乎所有常用警告

Windows 构建实际上是零警告模式——同一份代码在 Linux 上可能有数十个警告，在 Windows 上完全静默。

### 4.6 Windows 信号处理缺失

`engine.cc:219-238`：SIGINT/SIGTERM 被 `#ifndef _WIN32` 包裹。Windows 上运行的服务器无法优雅关闭。

### 4.7 生产代码中的测试代码

`engine.cc:249-279`：DB Service smoke test 内嵌在 `Engine::Start()` 中，每次 Debug 构建启动时执行。

### 4.8 原始裸指针管理 — RAII 缺失

绑定层统一使用 `new` 分配 Context 对象，通过 `RunInLoop` 延迟 `delete`：

```cpp
auto* ctx = new ServerCtx();        // 裸 new
// ...
// 在 __gc 或 stop/close 中:
loop->RunInLoop([del_ctx] { delete del_ctx; });  // 延迟 delete
```

这种"手动 new + 手动词 delete"的模式在 6 个绑定文件中出现约 15 次。MongoDB 绑定层的情况不同——使用 `new (std::nothrow)` + `delete`（在 `__gc` 中），且通过 `NewUserdata<T>` / `GetUserdata<T>` 模板统一管理。

### 4.9 Engine::Cleanup 生命周期顺序脆弱

`Cleanup` 中有 13 个顺序敏感的操作步骤。关键依赖关系是隐式的：

```cpp
// engine.cc:344        Physics Shutdown     (销毁物理 VM + 物理线程)
// engine.cc:348        DB Shutdown          (停止 DB 线程 + 连接池)
// engine.cc:359        ShutdownNetBindings  (需要 script_vm_ 的 Lua state 活着)
// engine.cc:362        ShutdownTimerBindings (需要 script_vm_)
// engine.cc:366-370    DestroyScript → lua_gc 查询 → 最终日志
```

`ShutdownNetBindings` 内部调用 `Engine::Instance().GetScriptVM().GetState()` 获取 Lua state 来 unref——但它依赖 `script_vm_` 尚未被销毁。如果将来有人调整 Cleanup 顺序或在 `ShutdownNetBindings` 之前释放 `script_vm_`，就会发生空指针解引用。这个顺序依赖没有被显式检查或文档化。

### 4.10 依赖管理

- 16+ 个第三方库全部 vendored，无包管理器
- 无 CI/CD 配置
- `copy_resources` 每构建全量复制

### 4.11 可观测性

- 无生产级 metrics
- 无 HTTP admin endpoint（`/health`、`/stats`）
- Perfetto 适合开发期 trace，不适合生产持续监控

---

## 第五部分：正面发现 — 已有良好实践

在全面分析过程中发现的值得肯定的设计决策：

### 5.1 PhysicsEngineBridge — 参考级架构

`PhysicsEngineBridge` 是代码库中设计质量最高的子系统：

- **逐方法线程验证**（`VerifyMainThread()`）：debug 下 assert + release 下 log
- **编译期访问控制**（`PHYSICS_INTERNAL_ACCESS` 宏）：`#error` 阻止外部直接包含 `physics_system.h`
- **SPSC 隔离**：MT → PT 用 SPSC 队列，零锁设计
- **API 边界清晰**：Bridge 是纯代理层，不包含业务逻辑

### 5.2 DBThread 线程模型文档

`db_thread.h` 的 [MT]/[DBT]/[SPSC]/[ATOM] 标注是工程中最完善的并发文档。

### 5.3 HTTP 绑定的 Shutdown 安全

`net_http_bind.cc` 展示了正确的异步回调 Shutdown 处理：`g_net_alive` atomic 标志 + mutex 保护 pending ref 列表 + TOCTOU 安全的清理交互。

### 5.4 MongoDB 绑定层的模板辅助

`bind_util.h` 中的 `GetUserdata<T>`/`NewUserdata<T>` 模板统一了全用户数据生命周期管理，消除了手动 `luaL_ref`/`luaL_unref` 的需要。46 个绑定文件都遵循相同模式。

### 5.5 Lua 侧封装质量

`server.lua` 和 `client.lua` 有状态追踪、pcall 安全回调、use-after-free 防护。

### 5.6 db_service_main_bind.cc 的输入验证

`db_send_request` 有完整的字段校验：类型检查、范围检查、枚举验证、清晰的错误消息。这是输入验证的参考实现。

---

## 第六部分：完备性总评

| 子系统 | 完备度 | 关键缺失 |
|--------|--------|---------|
| 事件循环 / 网络 IO | ★★★★☆ | 消息分帧、加密、限流、连接数管理、payload 上限 |
| 定时器 | ★★★★☆ | 层级过重、Lua 生命周期绑定 |
| 日志 | ★★★★☆ | 76 处 fprintf + std::cout 绕过日志系统 |
| 脚本 VM | ★★★☆☆ | 沙箱、协程、热更、错误恢复、循环依赖检测 |
| 脚本绑定 | ★★★☆☆ | 覆盖核心 API，80% 重复样板，RunInLoop 跨线程生命周期安全 |
| 配置管理 | ★★★☆☆ | 热通知缺失 — Reload 形同虚设 |
| 数据库 | ★★★☆☆ | ORM、缓存、多后端、cursor 泄漏、队列满静默丢弃 |
| 物理 | ★★☆☆☆ | 结果未接入游戏对象和网络同步 |
| **实体模型** | ★☆☆☆☆ | 无，仅 void* 数组 |
| **消息分帧** | ★☆☆☆☆ | 无，原始字节流直传 Lua |
| **多 VM 架构** | ★★☆☆☆ | 物理/DB 有独立 VM，业务层无 |
| **测试** | ★☆☆☆☆ | 仅 evpp 网络层，0 个 engine 层测试 |
| **跨线程安全** | ★★☆☆☆ | HTTP 绑定有正确模式；KCP/UDP/evpp 无 |
| **模块隔离** | ★☆☆☆☆ | 全局命名空间，无模块归属追踪 |
| AOI | ☆☆☆☆☆ | 无 |
| RPC | ☆☆☆☆☆ | 无 |
| 集群管理 | ☆☆☆☆☆ | 无 |
| 认证/安全 | ☆☆☆☆☆ | 无 |
| CI/CD | ☆☆☆☆☆ | 无 |

---

## 第七部分：优先级路线图

```
P0（现在就做 — 阻塞业务）：
  ┌──────────────────────────────────────────────────────────────┐
  │ 1. 消息分帧（Codec 层，~500 行 C++，先修 buffer.h 的两个 TODO）│
  │ 2. Entity 基类 + Component 模型（~1500 行 C++ + Lua）        │
  │ 3. 消息/负载大小限制 — 防 DoS（~50 行，所有 bind 添加检查）  │
  │ 4. 消除 4 个 abort() — 改为错误返回或异常                   │
  │ 5. 核心模块测试（ScriptVM → TimerManager → 集成测试）        │
  │ 6. 清理 76 处 fprintf(stderr) — 统一 Quill 日志              │
  └──────────────────────────────────────────────────────────────┘

P1（第一个里程碑 — 提升开发效率与安全性）：
  ┌──────────────────────────────────────────────────────────────┐
  │ 7. Lua 沙箱加固 — 移除 os/io/debug 或提供受限替代          │
  │ 8. luaL_error 异常安全 — 确保 C++ RAII 不被 longjmp 绕过    │
  │ 9. 多 VM 架构（Space-per-VM，复用 Physics 和 DBThread 模式）│
  │ 10. 协程集成（async/await 模式）                             │
  │ 11. 热更新系统                                               │
  │ 12. RPC 框架（基于 msgpack）                                 │
  │ 13. 配置热通知机制（Reload → 子系统回调）                    │
  │ 14. 绑定样板消除 — 将 bind_util.h 模式推广到网络绑定层      │
  └──────────────────────────────────────────────────────────────┘

P2（生产就绪）：
  ┌──────────────────────────────────────────────────────────────┐
  │ 15. AOI 系统                                                 │
  │ 16. ORM + 缓存层                                             │
  │ 17. 认证框架                                                 │
  │ 18. 监控指标 + Admin HTTP 接口                               │
  │ 19. Windows 信号处理补齐                                     │
  │ 20. UNIX/MSVC 编译器警告配置统一                             │
  │ 21. evpp Release 构建线程安全检查                             │
  │ 22. 数据库队列背压通知 + 请求丢弃日志                        │
  │ 23. msgpack 编码大小/深度限制                                 │
  │ 24. Lua 错误派发策略统一                                     │
  │ 25. RunInLoop 延迟 delete → weak_ptr/shared_ptr 迁移         │
  │ 26. 全局变量归属追踪 → ClearCache 清理全局                    │
  └──────────────────────────────────────────────────────────────┘

P3（持续完善）：
  ┌──────────────────────────────────────────────────────────────┐
  │ 27. 多数据库后端 + CI/CD                                     │
  │ 28. 消息优先级/限流 + 断线重连                               │
  │ 29. 代码重构（单例解耦、编译防火墙、TODO 清偿）              │
  │ 30. 清理内嵌测试代码（engine.cc DB smoke test）              │
  │ 31. Buffer 字节序问题修复                                    │
  │ 32. DNS resolver shared_ptr 泄漏修复                          │
  │ 33. Engine::Cleanup 生命周期顺序文档化 + 断言                 │
  └──────────────────────────────────────────────────────────────┘
```

---

## 第八部分：设计模式差距 — 同一代码库中的最佳与最差实践

### 对比表

| 维度 | PhysicsEngineBridge（最佳） | HTTP 绑定（优良） | 网络绑定 KCP/UDP（最差） | MongoDB 绑定（中间） |
|------|--------------------------|-------------------|------------------------|-------------------|
| 用户数据管理 | 不需要 — 使用现有类型系统 | 不需要 — g_net_alive + mutex + pending refs | 手动 lightuserdata + disposed | GetUserdata<T>/NewUserdata<T> 模板 |
| 生命周期清理 | 委托给 PhysicsSystem | ShutdownHttpBindings 原子接管 pending refs | 手动 luaL_unref + RunInLoop delete | __gc → delete obj; *ptr=nullptr |
| 访问控制 | PHYSICS_INTERNAL_ACCESS 编译期阻断 | 无 — 但作用域限于 static 函数 | 无 — 任何代码可直接 include | 无 |
| 线程安全 | VerifyMainThread + SPSC 队列 | g_net_alive atomic + TOCTOU safe pattern | 原始 lua_State* 跨线程捕获 + RunInLoop | 无特定保护 |
| Shutdown 安全 | 有序 Join + 队列排空 | g_net_alive=false → mutex 接管 pending → 安全 unref | disposed flag + 尝试 RunInLoop delete（loop 可能已停止） | 无 |
| 文档化 | 逐方法注释 + 线程模型图 | 函数级注释 + TOCTOU 推理 | 少量注释 | 少量注释 |

### 关键洞察

**代码库内部存在可复用的设计模式**——MongoDB 绑定层的 `bind_util.h` 模板系统如果被推广到网络绑定层，可以消除约 1500 行手工样板代码。PhysicsEngineBridge 的线程验证模式如果被推广，可以消除单例依赖。

网络绑定层的设计质量低不是因为"不知道怎么做"，而是因为**没有将已验证的模式横向推广**。

---

## 附录：定量数据汇总

| 指标 | 数值 |
|------|------|
| `fprintf(stderr, ...)` 调用数 | 76（13 个文件） |
| `std::cout` 调用（绕过日志的第三通道） | ~15（timer_manager.cc） |
| `abort()` / `std::abort()` 调用数 | 4 |
| TODO/FIXME/HACK/XXX 标记数 | 18 |
| 单例类数量 | 7 |
| `.Instance()` 调用次数 | 80+ |
| 协议绑定文件数 | 6 协议 × .cc + .h = 13 个文件 |
| 绑定样板代码量 | ~2000 行（占绑定总代码 ~80%） |
| `luaL_ref`/`luaL_unref` 调用数 | 60（6 个文件） |
| `disposed` 引用数 | 92（6 个文件） |
| `__gc`/`__index = mt` 注册数 | 30（13 个文件） |
| `NextAllString()` 调用点（无分帧） | 3（2 个 bind + tcp_conn.cc） |
| 消息大小无上限检查 | 全部 send() 路径 + msgpack pack |
| `RunInLoop` 捕获原始 lua_State* 的调用数 | 20+（4 个绑定文件） |
| `luaL_checklstring` 调用点（14 处，全部无长度上限） | 14（6 个 bind 文件） |
| MongoDB 绑定文件数 | 46 个 |
| Lua 脚本文件数 | 10 个 |
| Lua 全局导出函数数 | ~20+（log_* × 5 + timer × 3 + net × 7 + cmsgpack × 8 + mongo + db × 5） |
| evpp 层测试文件数 | ~15 个 |
| engine 层测试文件数 | **0** |
| 编译器警告禁用（MSVC） | 11 个 /wd flag |
| 物理结果接入行 | 0（全部注释掉） |
| 裸 new + 手动 delete 出现次数 | ~15（网络绑定层） |
| `reinterpret_cast` 调用数 | ~120（主要在 mongo 层 — C ABI 桥接） |
| catch(...) 块数 | ~20（大部分在 mongo 回调中防止 C 栈帧异常泄露） |
| `assert()` 调用总数 | ~120（主要在 evpp 层线程/状态验证） |
| Release 中禁用的线程安全检查 | 1 整套系统（inner_pre.cc evmap） |
| DNS resolver heap-alloc shared_ptr 泄漏风险 | 1（dns_resolver.cc:190） |
