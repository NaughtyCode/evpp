# RPC Bind — use-after-free SIGSEGV 修复（Release 构建）

**Date:** 2026-05-28
**Status:** Complete
**Commit:** `afbff6ec`

## Summary

修复 `rpc_bind.cc` 中 5 个 teardown 路径的 use-after-free 缺陷。
`RpcBindState::client_shared` / `server_shared` 中的 `shared_ptr` 是
`RpcClientCtx` / `RpcServerCtx` 的唯一所有者。在 `stop()` / `__gc` /
`ShutdownRpcBindings()` 中，`_shared.erase(ctx)` 在所有 `ctx->` 访问
完成之前被调用，导致对象被提前析构，后续访问为 use-after-free。

Debug 构建下，释放的内存被填充为哨兵模式 (0xDD)，访问通常不会立即崩溃。
Release 构建下，内存被回收或复用 → **SIGSEGV**。

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/script/rpc_bind.cc` | 将 `server_shared.erase()` / `client_shared.erase()` 移至各函数末尾 |

### 修复的函数

| 函数 | 问题 |
|------|------|
| `l_server_stop` | `server_shared.erase()` → 后续 `ctx->server.reset()`, `ctx->instance_ref` 等为 use-after-free |
| `l_server_gc` | 同上 |
| `l_client_stop` | `client_shared.erase()` → 后续 `ctx->client.reset()`, `DrainResponseQueue(ctx)`, `ctx->instance_ref` 等为 use-after-free |
| `l_client_gc` | 同上 |
| `ShutdownRpcBindings` (server 循环) | `server_shared.erase()` → 后续访问 `ctx->service_callbacks`, `ctx->server.reset()` 等为 use-after-free |

### 修复后的执行顺序

```
1. ctx->disposed = true
2. state->clients/servers.erase(ctx)     ← 阻止 UpdateRpcBindings 访问
3. 释放 Lua callback refs
4. Drain pending queue / response queue
5. client/server.reset()
6. 释放 instance_ref
7. state->client_shared/server_shared.erase(ctx)  ← 最后释放所有权
```

## Design Decisions

- **最小化变更**：仅调整执行顺序，不改变 `shared_ptr` 管理模式。
- 无需额外数据结构或 `atomic` 操作。
- 修复后 Release 构建 `test_rpc_bind`（34 用例）和 `test_rpc`（55 用例）均通过，无 SIGSEGV。
