# Task 9: Config Manager Unification

**Priority:** P1 — architecture
**Status:** pending
**Dependencies:** Task 1 (core-validation-fix), Task 2 (thread-safety-fix), Task 7 (physics-config-fix)

## Scope

Create `IConfigManager` interface. Unify ConfigManager and PhysicsConfigManager under it. Fix naming inconsistencies. Move `physics_scene_path` to PhysicsConfig.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P1-1 | 双配置管理器无统一抽象 |
| P1-5 | MongoDB配置使用camelCase（命名规范分裂） |
| P3-3 | `physics_scene_path`不应在RuntimeConfig |
| P3-11 | ConfigManager与PhysicsConfigManager校验模式不统一 |

## Key Changes

1. Define `IConfigManager` abstract interface: `Load()`, `Reload()`, `Validate()`, `GetSnapshot()`
2. Refactor `ConfigManager` to implement `IConfigManager`
3. Refactor `PhysicsConfigManager` to implement `IConfigManager`; add full `Reload()` method
4. Fix MongoDB config member naming: add `glaze::meta` mappings so C++ members use snake_case
5. Move `physics_scene_path` from `RuntimeConfig` to `PhysicsConfig`
6. Unify validation return types: all validators return `Result{valid, errors}` struct

## Affected Files

- New file: `src/runtime/config/i_config_manager.h`
- `src/runtime/config/config.h`
- `src/runtime/config/config.cc`
- `src/runtime/physics/physics_config.h`
- `src/runtime/physics/physics_config.cc`
- `src/runtime/engine/engine.cc`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
