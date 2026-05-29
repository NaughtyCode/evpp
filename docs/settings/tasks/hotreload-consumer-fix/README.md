# Task 3: Hot-Reload Consumer Fix

**Priority:** P0 — makes Reload functional
**Status:** done
**Dependencies:** Task 1 (core-validation-fix), Task 2 (thread-safety-fix)

## Scope

Make config reload actually propagate changes to engine subsystems. Add change-detail callback. Add config FileWatcher. Implement config rollback.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-3 | 配置热更后消费者不感知变更，Reload功能形同虚设 |
| P1-11 | 无配置回滚快照 |
| P1-12 | 配置文件无FileWatcher自动检测 |

## Key Changes

1. Redesign `ReloadCallback` to `std::function<void(const ConfigChangeSet&)>` with field-level change info
2. Wire reload to apply changes: frame_interval update, log level reconfiguration
3. Add `FileWatcher` for config files (reuse existing infrastructure from `ScriptReloader`)
4. Implement config rollback: keep previous config snapshot, expose `Rollback()` API
5. Add SIGHUP handler that triggers `ConfigManager::Reload()`

## Affected Files

- `src/runtime/config/config.h`
- `src/runtime/config/config.cc`
- `src/runtime/engine/engine.cc`
- `src/runtime/engine/engine.h`
- `src/tests/unit/config/test_config.cpp`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
