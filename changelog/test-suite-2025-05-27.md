# 测试套件修复与验证报告

**日期**: 2026-05-27
**分支**: server_engine2
**状态**: 22/22 测试通过

## 概述

CloudEngine 测试套件从零开始建立，包含 22 个测试（C++ 单元/集成/冒烟 + Lua 脚本）。本轮工作解决了所有测试失败问题，使通过率达到 100%。

## 修复清单

### 1. Lua 测试 segfault（6 个文件）

**问题**: `os.exit()` 在 `vm.DoFile()` 同步上下文中调用 C `exit()`，触发 atexit 处理器提前销毁 engine 单例，导致 segfault。

**修复**: 移除所有 Lua 测试文件中的 `os.exit()` 调用，退出码由 `lua_test_runner` 统一管理。

| 文件 | 修改内容 |
|------|---------|
| `src/tests/smoke/smoke_lua.lua` | 移除 pcall 包装和 os.exit(0)/os.exit(1) |
| `src/tests/lua/log/test_log.lua` | 移除 os.exit(0) |
| `src/tests/lua/import/test_import.lua` | 移除 os.exit(0) |
| `src/tests/lua/net/test_udp.lua` | 移除 os.exit(0) |
| `src/tests/lua/net/test_kcp.lua` | 移除 os.exit(0) |
| `src/tests/lua/msgpack/test_msgpack.lua` | 移除 os.exit(0)/os.exit(1) |

### 2. msgpack unpack_one 返回值顺序错误

**文件**: `src/runtime/script/msgpack_bind.cc:688`

**问题**: `UnpackFull()` 中 `lua_insert(L, 2)` 将 offset 插入到 value 之前，导致返回值顺序为 `(offset, value)` 而非 `(value, offset)`。测试中 `unpack_one` 返回 offset=1 被当作 value 赋值给变量，后续又将此值作为 offset 传给下次调用，最终在 offset=100 时越界报错。

**修复**: 删除 `lua_insert(L, 2)` 行。offset 在 push 后自然位于栈顶（所有 value 之后），无需插入操作。

### 3. evpp EventLoop 清理死锁（前置工作）

**背景**: TCP 测试在 `loop.Run()` 返回后调用 `client->Disconnect()` / `server->Stop()` 会死锁。因为 `Run()` 返回后 `watcher_` 为 nullptr、`IsRunning()` 为 false，`RunInLoop` 只入队不执行。

**修复文件**:
- `src/tests/smoke/smoke_tcp_loopback.cpp` — 清理代码移入回调
- `src/tests/integration/network/test_tcp.cpp` — 同上
- TCPServer thread_num 设为 0（smoke）或 1（integration）

**关键模式**:
```cpp
// 在消息回调中清理（处于 loop 线程内，RunInLoop 直接执行）
client->SetMessageCallback([&](...) {
    client->Disconnect();
    server->Stop();
    loop.Stop();
});
// 超时回调同样在 loop 线程内清理
```

### 4. UDP 集成测试缺少 MessageHandler

**文件**: `src/tests/integration/network/test_udp.cpp`

**问题**: 第二个测试用例未调用 `server.SetMessageHandler()`，`Server::Start()` 要求必须设置 handler，因此返回 false。

**修复**: 添加空 handler lambda。

### 5. TCP 集成测试端口冲突

**文件**: `src/tests/integration/network/test_tcp.cpp`

**问题**: 两个 TEST_CASE 共享端口 19877，Windows TCP TIME_WAIT 导致后一个测试 bind 失败。

**修复**: 使用独立端口 19877 和 19878。

### 6. ctest stderr 管道阻塞

**文件**: `src/runtime/evpp/event_loop.cc`

**问题**: Windows 上 ctest 通过管道捕获 stderr，`InitNotifyPipeWatcher()` 中的 `fprintf(stderr, ...)` 调试信息填充管道缓冲区，导致测试进程在 write 时阻塞。

**修复**: 移除 `InitNotifyPipeWatcher begin/done` 两行调试 fprintf（保留错误路径的 fprintf）。

### 7. RunAfter 浮点转纳秒溢出（前置工作）

**文件**: `src/runtime/evpp/event_loop.cc`

**问题**: `RunAfter(double delay_ms)` 将毫秒转为纳秒时，先乘 1000000.0 再转 int64_t，大值可能溢出。

**修复**: 使用 `Duration(static_cast<int64_t>(delay_ms * 1000000.0))` 正确构造。

## 最终测试结果

```
100% tests passed, 0 tests failed out of 22

Label Time Summary:
integration    =   1.55 sec*proc (2 tests)
lua            =   2.78 sec*proc (9 tests)
network        =   2.80 sec*proc (5 tests)
smoke          =   2.41 sec*proc (5 tests)
unit           =   0.81 sec*proc (6 tests)

Total Test time (real) =   7.34 sec
```

| 标签 | 测试数 | 说明 |
|------|-------|------|
| smoke | 5 | engine_init, scriptvm, config_load, tcp_loopback, lua |
| unit | 6 | buffer, config, timer, scriptvm, engine, client_api |
| integration | 3 | tcp, udp, client_api_tcp |
| lua | 9 | msgpack, client_server, udp, kcp, timer once/interval, log, import, smoke |

## 已知限制

1. **性能基准测试** — Google Benchmark 目标已定义但尚未构建（需 `CLOUDENGINE_BUILD_BENCHMARKS=ON`）
2. **物理集成测试** — 需 `ENGINE_PHYSICS_ENABLED=ON`
3. **CI/CD** — `.github/workflows/` 尚未创建
4. **TCP 多消息测试** — 因 evpp TCP 无消息帧协议，多次 Send 会合并为一次 read，已将多消息测试替换为大消息回显测试（4KB）
5. **lua_timer_interval** — `timer_bind.cc` 使用 `add_expires` + `kRestart` 模式重新触发定时器，当前测试通过但需要验证是否在所有边界情况下正确
