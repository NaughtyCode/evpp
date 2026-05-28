# Lua 绑定设计方案

## 设计目标

将 evpp 的网络、定时器、实体、空间、AOI、认证、RPC、数据库等子系统导出到 Lua，使脚本层能实现完整的游戏服务器逻辑。

## 模块总览

```
net             — 网络模块容器
  .client       — TCP 异步客户端
  .server       — TCP 多线程服务端（连接/消息回调）
  .http         — HTTP 异步客户端
  .udp_client   — UDP 同步客户端
  .udp_server   — UDP 服务端
  .kcp_client   — KCP 可靠 UDP 客户端
  .kcp_server   — KCP 可靠 UDP 服务端
entity          — 实体创建与管理
space           — 空间管理
aoi             — 兴趣区域
timer           — 定时器
log_*           — 日志（6 个全局函数）
cmsgpack        — MessagePack 编解码
auth            — 认证与会话管理
rpc             — RPC 客户端/服务端
import          — 模块导入
orm             — ORM 抽象层（MongoDB）
db_*            — 数据库服务全局函数（MongoDB）
bson / mongoc   — MongoDB 底层驱动（MongoDB）
mem             — 内存统计（可选）
```

## 核心设计决策

1. **Instance Table 模式**: 所有长生命周期对象（client/server/entity/rpc_server/etc）返回 Lua 实例表，通过 `_ctx` 字段存储 C++ 上下文指针，通过 metatable 提供方法

2. **共享 EventLoop**: 所有网络对象复用 Engine 的主 EventLoop，避免多线程复杂性

3. **回调通过 set_on_* 设置**: 回调存储在 Lua 实例表上，通过 `luaL_ref` 注册到 registry 中持久化

4. **GC 安全**: `__gc` metamethod 确保 Lua 对象被回收时 C++ 资源得到释放

5. **变量参数的回调**: 连接回调和服务器回调在不同的粒度上工作——
   - Server 级回调接收连接实例作为参数: `on_connect(conn_inst, remote_addr)`
   - Connection 级回调覆盖 Server 级: `conn:set_on_message()` > `server:set_on_message()`

## 关键模式

### Instance Table 模式

```cpp
// C++ 侧: 创建 Lua 实例表
PushInstanceTable(L, ctx, kMetaName);  // 返回 {_ctx = lightuserdata, metatable}
// 实例表存储在 Lua registry 中用于回调
ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);

// 方法通过 metatable __index 分发
// kMethods = {{"send", l_send}, {"close", l_close}, ...}
RegisterInstanceMeta(L, kMetaName, kMethods, l_gc);

// 方法实现从实例表提取 ctx
auto* ctx = GetCtxFromTable<MyCtx>(L, 1);
```

### 回调模式

```cpp
// C++ 侧: 从 registry 获取实例表，调用其上的方法
CallInstMethod(L_ptr, inst_ref, "on_connect");           // fn()
CallInstMethodStr(L_ptr, inst_ref, "on_message", data);  // fn(data)
```

```lua
-- Lua 侧: 直接在实例表上设置回调函数
client.on_connect = function()
    print("connected")
end
-- 或使用便捷方法
client:set_on_connect(function() ... end)
```

### 生命周期

```
Lua GC → __gc(l_xxx_gc) → ctx->disposed = true → 清理 C++ 资源
手动停止 → instance:stop() → 同 GC 路径
引擎关闭 → ShutdownXxxBindings() → 遍历所有活跃实例 → 逐一清理
```

## 各模块设计细节

### net.client (TCP 客户端)

- **设计**: `evpp::TCPClient` 包装，通过 `LengthPrefixedCodec` 进行消息帧封装
- **回调**: `on_connect()`, `on_message(data)`, `on_close()`
- **全局跟踪**: `g_client_ctxs` map + `g_client_shared` 管理引用计数，确保关闭时正确清理
- **自动重连**: 禁用（`set_auto_reconnect(false)`），由 Lua 层实现重连逻辑

### net.server (TCP 服务端)

- **设计**: `evpp::TCPServer` 包装，thread_num=0 在主事件循环中处理所有连接
- **两级回调**: Server 级回调作为默认，Conn 级回调可覆盖
- **连接管理**: 新连接时创建 `ConnCtx` 实例表，存储在 TCPConn 的 context 中用于 O(1) 分发
- **重入保护**: `disposed` 标志防止关闭回调的重入问题

### net.http (HTTP 客户端)

- **设计**: `evpp::httpc::Request` 包装，通过 `NetAliveGuard` 原子标志 + 互斥锁保护
- **待处理引用跟踪**: `g_http_pending_refs` + `g_http_mutex` 确保关闭时释放所有回调引用
- **TOCTOU 安全**: 在回调分发和关闭路径之间

### UDP 客户端/服务端

- **UDP 客户端**: `evpp::udp::sync::Client` 同步包装，支持 send/do_request 两种模式
- **UDP 服务端**: `evpp::udp::Server` 异步包装，recv 线程通过 `RunInLoop` 回调主线程
- **多端口监听**: 支持单端口（数字）或多端口（"p1,p2" 字符串）

### KCP 客户端/服务端

- **KCP 客户端**: `evpp::kcp::sync::Client` 同步包装，支持调优（nodelay/wnd_size/mtu/conv）
- **KCP 服务端**: `evpp::kcp::Server` 异步包装，类似 UDP 服务端的线程模型
- **两步创建**: `new()` + 调优 + `connect()` 模式支持在连接前配置 KCP 参数

### timer

- **设计**: 基于 `TimerManager` 的抽象，支持超时和周期两种模式
- **每 VM 隔离**: 每个 ScriptVM 有独立的 `TimerBindState`
- **周期取消检测**: 回调内可使用 `ctx->ref == LUA_NOREF` 检测是否已取消

### entity (实体系统)

- **设计**: 基于 `EntityManager` 的实体抽象，每个实体有动态属性和 Lua 组件
- **属性系统**: 使用 `std::variant<int64_t, double, std::string, bool>` 作为属性值
- **组件系统**: Lua 组件以 Registry ref 形式存储，支持动态挂载/卸载
- **连接绑定**: 实体可绑定到 TCP 连接，`entity:send()` 自动委托到 `conn:send()`

### space (空间系统)

- **设计**: 基于 `SpaceManager` 的空间隔离层，支持多空间
- **跨空间消息**: 通过 `SpaceMessageRouter` 实现异步跨空间通信
- **消息队列**: `_pending_messages` 表实现 Lua 侧的消息轮询

### rpc (RPC 系统)

- **设计**: 双层架构 — Lua 侧的回调通过 deferred queue + `UpdateRpcBindings()` 驱动
- **传��层解耦**: `client:set_send_callback()` 允许任意传输层（TCP/KCP/HTTP）
- **时间预算**: 服务器 pending queue 处理有 5ms 时间预算限制，防止帧卡顿
- **5 秒超时**: 服务端 handler 执行有 5 秒超时
- **Shared Ownership**: `shared_ptr` + `weak_ptr` 确保传输层回调安全

### orm (ORM 层)

- **Schema 注册**: 支持字段类型定义（int/double/bool/string）和索引定义
- **查询抽象**: 基于 JSON 字符串的 CRUD 操作
- **缓存**: 内置命中和未命中统计

### db_service (数据库服务)

- **异步架构**: 基于独立 DB 线程 + SPSC 队列的异步请求/响应模型
- **操作码**: 支持 find/find_one/insert_one/insert_many/update_one/update_many/delete_one/delete_many/count/aggregate/command/execute_script
- **轮询模型**: `db_poll_response()` 非阻塞轮询，每帧调用一次

### import (模块系统)

- **设计**: 基于 `ScriptImporter` 的自定义 require 替代方案
- **可调用表**: 使用 `__call` metamethod 实现 `import("mod")` 语法
- **路径管理**: 独立的搜索路径，不依赖 Lua 的 `package.path`

## 关闭顺序

引擎关闭时的清理流程：

```
ShutdownNetBindings()
  → ShutdownHttpBindings()    (先阻止新 HTTP 回调)
  → ShutdownClientBindings()  (断开所有 TCP 客户端)
  → ShutdownServerBindings()  (停止所有 TCP 服务端)
  → ShutdownUdpServerBindings() (停止所有 UDP 服务端)
  → ShutdownKcpServerBindings() (停止所有 KCP 服务端)

ShutdownRpcBindings()         (清理 RPC 实例)
ShutdownEntityBindings()      (销毁所有实体)
ShutdownTimerBindings()       (取消所有定时器)
```

## 错误处理原则

- 网络操作中的无效 ctx 返回 `luaL_error` (Lua 异常)
- 连接关闭后的操作返回 `luaL_error`
- HTTP/RPC 回调错误被捕获、记录日志后安全忽略（不回传到调用方）
- `cmsgpack_safe` 模块提供 `nil, errmsg` 风格的错误返回
