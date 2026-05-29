# Task 6: Health Check Probes

**Priority:** P0 — container orchestration
**Status:** done
**Dependencies:** Task 1 (core-validation-fix), Task 4 (runtime-environment-selection)

## Scope

Differentiate startup/readiness/liveness probes. Validate backend dependencies in health checks. Expose cleanup phase externally.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-6 | 健康检查不验证后端依赖，K8s无法正确判断Pod健康 |
| P3-8 | CleanupPhase不对外可见 |
| P3-9 | 缺少Startup/Readiness/Liveness探针区分 |

## Key Changes

1. Added three distinct endpoints: `/health/startup`, `/health/readiness`, `/health/liveness` in `admin_http.cc`
2. Startup probe: returns 503 until `Engine::Init()` completes (`initialized_` atomic), then 200
3. Readiness probe: validates DB connection pool, physics thread, MongoDB connectivity with nested JSON status
4. Liveness probe: lightweight check (running + initialized + uptime + frame_count)
5. Added `/health/phase` endpoint exposing `CleanupPhase` for load balancer drain coordination
6. Structured error codes with per-dependency `{"status": "...", "message": "..."}` objects
7. `Engine::initialized()` and `Engine::cleanup_phase()` public accessors
8. `PhysicsEngineBridge::IsInitialized()` added (delegates to PhysicsSystem)

## Affected Files

- `src/runtime/network/admin_http.cc`
- `src/runtime/network/admin_http.h`
- `src/runtime/engine/engine.h`
- `src/runtime/engine/engine.cc`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
