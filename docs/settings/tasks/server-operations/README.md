# Task 8: Server Operations

**Priority:** P1 — production readiness
**Status:** done
**Dependencies:** Task 1 (core-validation-fix), Task 2 (thread-safety-fix), Task 3 (hotreload-consumer-fix)

## Scope

Graceful connection draining. Configurable shutdown timeout. PID file management. Structured exit codes. Configurable resource limits. TCP keepalive config.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P1-6 | ProfilerConfig硬编码 |
| P1-7 | ResourceLimits全部为编译期常量 |
| P1-9 | 无连接优雅排空 |
| P1-10 | 无可配置关闭超时 |
| P1-13 | 连接数上限硬编码10000 |
| P2-6 | 无PID文件/多实例防护 |
| P2-7 | 启动失败无结构化错误码 |
| P3-2 | Config热更与Script热更无协调 |
| P3-13 | 无实例身份标识配置 |
| P3-19 | TCP keepalive参数不可配置 |

## Key Changes

1. Add `shutdown_timeout_sec` to `ServerConfig` (default 30s)
2. Implement graceful connection draining: stop accepting → drain existing → close
3. Add PID file writing with file lock for multi-instance prevention
4. Define structured exit codes: 0=OK, 1=generic, 2=config missing, 3=parse error, 4=validation fail, 5=permission
5. Move `ResourceLimits` from `static constexpr` to runtime-configurable fields in `ServerConfig`
6. Add TCP keepalive config (idle, interval, count) to network config
7. Add `max_connections` config field (replace hardcoded 10000)
8. Add `instance.id`, `instance.region`, `instance.zone` identity fields to `ServerConfig`

## Affected Files

- `src/runtime/engine/engine.cc`
- `src/runtime/engine/engine.h`
- `src/server/server.cc`
- `src/runtime/config/limits.h`
- `src/runtime/config/config.h`
- `src/runtime/network/tcp_server.h`
- New file: `src/runtime/core/pid_file.h`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
