# Verification Checklist: Health Check Probes

## Unit Tests

- [ ] `/health/startup` returns 503 before `Engine::Init()` completes
- [ ] `/health/startup` returns 200 after `Engine::Init()` completes
- [ ] `/health/readiness` returns 200 when all dependencies healthy
- [ ] `/health/readiness` returns 503 when DB connection pool unhealthy
- [ ] `/health/readiness` returns 503 when physics engine not initialized
- [ ] `/health/readiness` returns 503 when MongoDB unreachable
- [ ] `/health/liveness` returns 200 while event loop runs
- [ ] `/health/phase` returns current CleanupPhase string
- [ ] During shutdown, readiness returns 503 (draining)
- [ ] Health response includes per-dependency status JSON

## Integration Tests

- [ ] Start server → poll `/health/startup` → transitions 503→200 after Init
- [ ] Kill MongoDB → `/health/readiness` transitions to 503
- [ ] Send SIGTERM → `/health/phase` shows progressing cleanup phases

## Manual Verification

- [ ] `curl http://localhost:8081/health/startup` → `{"status":"starting"}`
- [ ] `curl http://localhost:8081/health/readiness` → `{"status":"ready","checks":{...}}`
- [ ] `curl http://localhost:8081/health/liveness` → `{"status":"alive"}`
