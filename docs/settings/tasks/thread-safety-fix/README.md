# Task 2: Thread Safety Fix

**Priority:** P0 — data race elimination
**Status:** done (2026-05-29)
**Dependencies:** Task 1 (core-validation-fix)

## Scope

Fix `LoadRuntimeFromString` data race. Remove public mutable accessors. Simplify callback mutex. Add TOCTOU-safe compound API.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-2 | `LoadRuntimeFromString`无锁写入，与`GetRuntimeConfig()`数据竞争（UB） |
| P1-3 | 可变访问器绕过线程安全 |
| P3-1 | `callbacks_mutex_`的`shared_mutex`可简化为`std::mutex` |

## Key Changes

1. Add `std::lock_guard` to all `Load*FromString` methods
2. Remove `GetRuntimeConfigMutable()`, `GetClientConfigMutable()`, `GetServerConfigMutable()`
3. Replace `callbacks_mutex_` `std::shared_mutex` with plain `std::mutex`
4. Add TOCTOU-safe compound API for MongoDB config loading

## Affected Files

- `src/runtime/config/config.h`
- `src/runtime/config/config.cc`
- `src/server/server.cc`
- `src/tests/unit/config/test_config.cpp`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
