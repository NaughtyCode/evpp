# Task 4: Runtime Environment Selection

**Priority:** P0 — compile-time → runtime switch
**Status:** done
**Dependencies:** Task 1 (core-validation-fix), Task 3 (hotreload-consumer-fix)

## Scope

Replace `#ifndef NDEBUG` MongoDB selection with runtime environment profile. Add profile hierarchy (common → {env} → CLI override).

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-4 | Dev/Prod DB选择是编译期行为，同一二进制无法多环境部署 |
| P1-14 | 无配置profile层级系统（dev/staging/prod） |
| P3-10 | 缺少配置包含/覆盖机制 |

## Key Changes

1. Add `environment` field to `RuntimeConfig` (enum: `development`, `staging`, `production`)
2. Add `--env=` CLI flag and `EVPP_ENV` environment variable support
3. Replace `#ifndef NDEBUG` check in `engine.cc:178-182` with runtime environment check
4. Implement config profile layering: `common.json` → `{env}.json` → CLI/env-var overrides
5. Add `mongodb_active` field to `ServerConfig` (replaces dual dev/public selection)

## Affected Files

- `src/runtime/engine/engine.cc`
- `src/runtime/config/config.h`
- `src/runtime/config/config.cc`
- `src/server/server.cc`
- `src/runtime/config/config_constants.h`
- `resources/config/` (new profile JSON files)

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
