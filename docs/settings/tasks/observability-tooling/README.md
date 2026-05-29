# Task 12: Observability & Tooling

**Priority:** P2 — ops quality
**Status:** pending
**Dependencies:** Task 1 (core-validation-fix), Task 3 (hotreload-consumer-fix)

## Scope

Enhanced reload logging with field-level diff. Config snapshot/export API. Prometheus config metrics. Dry-run validation mode. Standalone CLI validator. Config change webhook.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P2-1 | Reload日志不含变更详情 |
| P2-2 | 无配置导出/快照API |
| P2-3 | 无dry-run校验模式 |
| P2-5 | 三种JSON键命名风格并存 |
| P2-10 | 缺少Prometheus配置相关指标 |
| P2-11 | 无独立config-validate CLI工具 |
| P2-12 | 配置变更无Webhook/事件通知 |

## Key Changes

1. Add field-level diff logging in `Reload()`: log old→new values for each changed field
2. Implement `ConfigManager::Dump()` → JSON snapshot of running config
3. Add Prometheus metrics: `config_reload_total`, `config_reload_errors_total`, `config_reload_duration_seconds`, `config_hash`
4. Add `ConfigManager::ValidateOnly(path)` dry-run mode (parse + validate, don't apply)
5. Build `evpp-config-validate` CLI tool (standalone binary) for CI/CD
6. Add config change webhook: POST config hash + changed fields to configured URL after Reload
7. Add startup failure structured error output to stderr

## Affected Files

- `src/runtime/config/config.cc`
- `src/runtime/config/config.h`
- New file: `src/tools/config_cli.cpp`
- `src/runtime/network/admin_http.cc`
- `src/runtime/core/metrics.h`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
