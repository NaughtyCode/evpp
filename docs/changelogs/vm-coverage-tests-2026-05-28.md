# VM 覆盖率测试 — test_vm_coverage.cpp

**Date:** 2026-05-28
**Status:** Complete
**Commit:** `afbff6ec`

## Summary

新增 `test_vm_coverage.cpp`，覆盖 8 个之前完全没有测试的 Lua VM 功能模块，
共计 39 个测试用例、111 个断言，全部通过。

## Coverage Matrix

| 模块 | 新增测试数 | 覆盖内容 |
|------|-----------|---------|
| **VMCustomPtrStore** | 8 | SetNull, CopyTo, CopyFrom, Reserve/Capacity, Empty, Set(gaps), 越界 no-op |
| **ScriptVM** | 8 | DoDirectory, RegisterFunctions, RegisterModuleOpen, RegisterCallback, ToString, LuaVersion |
| **ScriptImporter** | 5 | SetPaths, AddPath, ClearCache, wildcard (dir.*), circular dependency |
| **FileWatcher** | 3 | 构造/IsRunning, Start/Stop 生命周期, 回调触发 |
| **ScriptReloader** | 3 | 空停止安全, SetTarget/Start/Stop, ReloadFile 失败路径 |
| **bind_util** | 6 | LuaError, PushLibrary, PushInstanceTableShared (GC 生命周期 + scope 存活), CallInstMethod, CallInstMethodStr |
| **lua_error_handler** | 6 | PushLuaErrorHandler, SafeCallLua (NotFound/Ok/Error/自定义 handler), ShouldLogError |

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/tests/unit/vm/test_vm_coverage.cpp` | 新增 39 个测试用例 |
| `src/tests/unit/CMakeLists.txt` | 添加 `test_vm_coverage` target + CTest 注册 |

## 发现并修复的 Bug

测试编写过程中发现并修复了 4 个生产代码 Bug：

| 文件 | Bug | 修复 |
|------|-----|------|
| `lua_error_handler.h` | `SafeCallLua` 空栈 UB + lua_remove 双重删除 | func_idx 守卫 + 栈管理重写 |
| `coroutine_scheduler.cc` | `lua_isfunction` 空栈 UB | 前置 gettop 检查 |
| `coroutine_scheduler.cc` | `lua_xmove` 弹出 thread ref 而非 function | 添加 lua_insert 旋转 |
| `coroutine_scheduler.cc` | `lua_resume` nargs 计算错误 | 改为 top-1 |

## Design Decisions

- 每个测试使用 `ScriptVMFixture` 或独立 `lua_State*` 保证隔离
- 临时目录使用 `std::filesystem` 管理，测试后自动清理
- 时间敏感的测试（FileWatcher 回调）使用 `CHECK` 替代 `REQUIRE` 避免 CI 抖动
