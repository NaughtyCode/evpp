# Task 11: Game Business Config Framework

**Priority:** P1 — Lua/game logic blocker
**Status:** done
**Dependencies:** Task 1 (core-validation-fix), Task 3 (hotreload-consumer-fix)

## Scope

Implement Lua-layer config API. Add data table loading (CSV/JSON). Implement reference integrity validation. Add dual-end config sync infrastructure.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P1-15 | Lua层无游戏业务配置框架 |
| P1-16 | 无配置引用完整性校验 |
| P2-14 | Lua层无config API |
| P2-16 | 客户端-服务器配置无同步机制 |
| P2-17 | 游戏业务配置无导表工具链 |
| P2-18 | 无配置跨系统引用管理 |

## Key Changes

1. Implement Lua C API: `config.get(path)`, `config.get_module(name)`, `config.on_change(module, callback)`
2. Add `ConfigTable` class: loads CSV/JSON data tables with type inference and validation
3. Implement reference integrity checker: validates cross-file ID references
4. Add config loading order with topological sort (dependencies loaded first)
5. Add `SandboxLevel` enum (replace free-form string)
6. Create example data table schemas: `monster.json`, `item.json`, `drop_table.json`

## Affected Files

- New file: `src/runtime/config/config_bind.cpp` (Lua bindings)
- New file: `src/runtime/config/config_table.h`
- New file: `src/runtime/config/config_table.cc`
- New file: `src/runtime/config/reference_validator.h`
- New file: `src/runtime/config/reference_validator.cc`
- `src/runtime/config/config.h` (SandboxLevel enum)
- `resources/script/data/` (new data directory)

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
