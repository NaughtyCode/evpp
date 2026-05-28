# Changelog — RPC Bind 重写（light userdata + deferred dispatch）

## [Unreleased] — 2026-05-28

### Fixed — RPC 绑定的 6 个严重缺陷

原 `rpc_bind.cc` 使用文件作用域全局变量 `g_rpc_server`/`g_rpc_client`，handler
lambda 直接从网络线程调用 `lua_pcall`，存在以下严重缺陷：

1. **跨线程调用 Lua（CRITICAL）**：`RpcServer::RegisterService` 的 handler 在
   网络线程中被 `HandleRequest()` 调用，直接执行 `lua_pcall`。Lua state 非线程安全，
   会导致 VM 损坏/崩溃。

2. **全局单例**：`g_rpc_server`/`g_rpc_client` 为文件作用域全局变量，多个 ScriptVM
   实例共享同一 server/client，且 handler 通过 `Engine::Instance().GetScriptVM()`
   忽略 `ExportRpc(ScriptVM&)` 传入的 VM 参数。

3. **luaL_ref 泄漏**：`stop_server()` 调用 `g_rpc_server.reset()` 销毁所有 handler，
   但 handler lambda 中捕获的 `cb_ref` 从未 `luaL_unref`，Lua registry slot 泄漏。

4. **Lua 错误静默吞噬**：handler lambda 中 `lua_pcall` 失败后仅 `lua_pop` 不记录日志，
   调用方收到默认 `"{}"` 返回值，完全丢失错误信息。

5. **无 Shutdown 函数**：缺少 `ShutdownRpcBindings()`，引擎关闭时 registry ref
   泄漏，server/client 未正确清理。

6. **无延迟分发机制**：缺少 `UpdateRpcBindings()` 将 RPC handler 回调延迟到主线
   VM 线程执行。

### Changed — Light Userdata 绑定模式

完全重写 `rpc_bind.cc`，采用与 `net_tcp_server_bind.cc` 一致的 light userdata +
metatable 模式：

- **Per-VM 状态**：`RpcBindState` 存储于 Lua registry `__RpcBindState`，追踪当前
  VM 所有 server/client 上下文，支持 shutdown 批量清理。
- **Lua 对象模型**：`rpc.new_server()` / `rpc.new_client()` 返回带 `_ctx`
  lightuserdata + metatable 的 Lua table，方法通过 `server:method()` / `client:method()`
  调用，`__gc` 自动清理。
- **线程安全**：handler lambda 使用 `std::weak_ptr<RpcServerCtx>` + `std::promise`/
  `std::future` 延迟队列。handler 在网络线程将请求入队并阻塞在 `future.get()`；
  `UpdateRpcBindings()` 在主线程排队队列、调用 Lua callback、fulfill promise。
- **生命周期安全**：`l_server_stop` / `l_server_gc` / `ShutdownRpcBindings` 使用
  `alive → shared_shared.erase → loop-drain → server.reset` 顺序，确保所有
  in-flight handler 返回后才销毁 server。
- **错误传播**：Lua callback 错误通过 `ENGINE_LOG_ERROR` 记录并以
  `{"error":"..."}` JSON 返回给调用方。

### Changed — Lua API

| 原 API | 新 API |
|--------|--------|
| `rpc.start_server()` → bool | `rpc.new_server()` → server_table |
| `rpc.register_service(name, cb)` → bool | `server:register_service(name, cb)` → bool |
| (无) | `server:unregister_service(name)` → bool |
| `rpc.stop_server()` → bool | `server:stop()` → bool |
| `rpc.start_client()` → bool | `rpc.new_client()` → client_table |
| `rpc.call(svc, method, args, timeout)` → resp | `client:call(svc, method, args, timeout)` → resp |
| `rpc.stop_client()` → bool | `client:stop()` → bool |

Lua callback 签名从 `(method, body)` 修正为 `(service, method, body)`，与文档注释一致。

### Added — UpdateRpcBindings / ShutdownRpcBindings

- `ExportRpc(vm)` — 创建 per-VM 状态、注册 metatable、导出 `rpc` 模块
- `UpdateRpcBindings(vm)` — 每帧在 `FrameLoop` 中调用，排队所有 server 的延迟队列
- `ShutdownRpcBindings(vm)` — 引擎关闭时在 `ShutdownNetBindings` 之前调用，
  释放所有 Lua ref、安全销毁 server/client

### Added — 自动化测试

- 新建 `src/tests/unit/script/test_rpc_bind.cpp`：RPC Lua 绑定单元测试，覆盖
  server/client 创建、service 注册/取消注册、延迟分发、error 处理、stop/GC、
  UpdateRpcBindings 等场景。

### Related files

| 文件 | 变更 |
|------|------|
| `src/runtime/script/rpc_bind.h` | 新增 `UpdateRpcBindings`、`ShutdownRpcBindings` 声明 |
| `src/runtime/script/rpc_bind.cc` | 完全重写（146→565 行），light userdata + deferred queue |
| `src/runtime/script/script_bind.h` | 新增两个声明 |
| `src/runtime/engine/engine.cc` | `FrameLoop` 中调用 `UpdateRpcBindings`，`Cleanup` 中调用 `ShutdownRpcBindings` |
| `src/tests/unit/script/test_rpc_bind.cpp` | 新建 RPC 绑定测试 |
| `src/tests/unit/CMakeLists.txt` | 注册 `test_rpc_bind` |
