# lua_error_handler — SafeCallLua 空栈 / lua_remove 双重删除修复

**Date:** 2026-05-28
**Status:** Complete
**Commit:** `afbff6ec`

## Summary

`lua_error_handler.h` 中的 `SafeCallLua` 函数存在两个 Bug：

1. **空栈 UB**：`func_idx = lua_gettop(L) - opts.nargs`，栈空时 `func_idx = 0`，
   随后 `lua_isfunction(L, 0)` 对 Lua 栈索引 0 操作是 UB（Release 下可能触发 PANIC）。

2. **双重删除 error handler**：`lua_pcall` 的 `msgh` 参数会自行移除 error handler
   （成功时保留在 func_idx 处，失败时被调用并消耗）。但旧代码在成功和失败路径
   都额外调用 `lua_remove(L, func_idx)`，导致成功时删除了返回结果、失败时
   从空栈删除触发 PANIC。

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/vm/lua_error_handler.h` | 重写 `SafeCallLua` 的栈管理逻辑 |

### 修复详情

- 增加 `func_idx <= 0` 守卫，空栈/浅栈直接返回 `NotFound`
- 错误路径：使用 `lua_settop(L, func_idx - 1)` 彻底清理 error handler + error message
- 成功路径：使用 `lua_remove(L, func_idx)` 仅移除 error handler（`lua_pcall` 保留它），结果下移
- 添加 `#include <algorithm>` 以使用 `std::min`

## Design Decisions

- 由于该函数之前无调用者（inline in header 且未被 include），修复对现有代码无影响
- 新增的 `test_vm_coverage.cpp` 中的 `[luaerr]` 测试覆盖了全部 5 个 SafeCallLua 分支
