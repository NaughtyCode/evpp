# CoroutineScheduler — 3 处 Bug 修复

**Date:** 2026-05-28
**Status:** Complete
**Commit:** `afbff6ec`

## Summary

`coroutine_scheduler.cc` 中存在 3 个 Bug，均导致 SIGSEGV：

1. **空栈调用 `lua_isfunction`**：`CreateCoroutine` 在检查栈顶是否是函数前
   未校验 `lua_gettop(L) > 0`，空栈时 `lua_isfunction(L, -1)` 是 UB。

2. **`lua_xmove` 弹出错误元素**：`lua_newthread(L)` 将 thread ref 推入栈顶，
   原函数位于 -2。随后 `lua_xmove(L, thread, 1)` 从栈顶弹出 1 个元素
   （thread ref，而非 function），导致 thread 空栈、后续 `lua_resume` 崩溃。

3. **`lua_resume` 的 `nargs` 计算错误**：旧代码 `lua_gettop(cs.thread) > 0 ? 1 : 0`
   将 function 本身计入 nargs。应计算 `gettop - 1`（总元素数减 function）。

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/vm/coroutine_scheduler.cc` | 修复 `CreateCoroutine` 和 `Update` |

### 修复详情

**`CreateCoroutine`：**
- 增加 `lua_gettop(L) == 0` 前置检查，空栈立即返回 0
- 增加 `lua_insert(L, -2)` 将 function 旋转至栈顶后再 `lua_xmove`
- 移除 error path 中的 `GetLogger()` 调用（测试环境可能未初始化 logger）

**`Update`：**
- `nargs` 从 `top > 0 ? 1 : 0` 改为 `top > 0 ? top - 1 : 0`

## Design Decisions

- `nargs` 计算遵循 Lua 规范：coroutine 栈上 function 位于栈底，args 在其上，
  `lua_resume` 的 nargs 参数仅为 args 数量（不含 function）
- `lua_insert` 在 `lua_newthread` 后旋转栈顺序（原本就应如此），
  thread ref 保留在主栈上由 Lua GC 管理
