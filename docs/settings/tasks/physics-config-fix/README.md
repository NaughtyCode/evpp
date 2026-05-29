# Task 7: Physics Config Fix

**Priority:** P0 — silent data loss
**Status:** pending
**Dependencies:** None (PhysicsConfigManager is independent)

## Scope

Enable `error_on_unknown_keys` for PhysicsConfigManager. Unify unknown-key behavior with ConfigManager. Fix directory naming inconsistency.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-7 | Physics JSON静默忽略未知键，拼写错误无声丢弃 |
| P3-6 | 物理配置目录用复数`configs/`，命名不统一 |
| P3-11 | ConfigManager与PhysicsConfigManager未知键处理不一致 |

## Key Changes

1. Change `glz::opts{.error_on_unknown_keys = false}` to `true` in all PhysicsConfigManager loaders
2. Add schema documentation for physics JSON files
3. Rename physics config directory from `configs/` to `config/` (consistency)
4. Add version field (`"version": 1`) to physics config files for schema migration
5. Enable `error_on_missing_keys = true` with warning logs

## Affected Files

- `src/runtime/physics/physics_config.cc`
- `src/runtime/physics/physics_config.h`
- `resources/physics/configs/` → `resources/physics/config/` (rename)
- All 4 physics JSON files
- `src/runtime/engine/engine.cc` (update path reference)

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
