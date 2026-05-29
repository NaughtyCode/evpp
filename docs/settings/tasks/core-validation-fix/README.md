# Task 1: Core Validation Fix

**Priority:** P0 — blocks all other config work
**Status:** pending
**Dependencies:** None

## Scope

Wire `ConfigValidator::Validate()` into all Load/Reload paths. Expand validation coverage to ServerConfig and ClientConfig. Add cross-field validation.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-1 | `ConfigValidator::Validate()`从未被调用 |
| P1-4 | 无跨字段校验 |
| P3-11 | ConfigManager与PhysicsConfigManager未知键处理不一致 |

## Key Changes

1. Call `ConfigValidator::Validate()` in `Load()` and `Reload()` after parsing, before applying config
2. Extend `ConfigValidator` to validate `ServerConfig` fields (admin_port, http.timeout_sec, msgpack limits)
3. Add cross-field checks: `interval_ms` vs `target_fps` consistency
4. Add `sandbox_level` enum validation (reject unknown sandbox strings)
5. Add warnings for `error_on_missing_keys` (fields silently using defaults)

## Affected Files

- `src/runtime/config/config_validator.h`
- `src/runtime/config/config_validator.cc`
- `src/runtime/config/config.cc`
- `src/runtime/config/config.h`
- `src/tests/unit/config/test_config.cpp`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
