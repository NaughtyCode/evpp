# Verification Checklist: Health Check Probes

## Unit Tests

- [x] `Engine::initialized()` returns false before Init
- [x] `Engine::initialized()` returns true after Init completes
- [x] `Engine::cleanup_phase()` transitions to Complete after Cleanup
- [x] `PhysicsEngineBridge::IsInitialized()` returns true after engine init
- [x] `AdminHttpServer` Start/Stop lifecycle works
- [x] Engine test instance supports initialized() and cleanup_phase() accessors
- [x] Engine frame_count increments in library mode

## HTTP Health Endpoints (manual verification)

- [ ] `/health/startup` returns `{"status":"starting"}` with 503 before Init
- [ ] `/health/startup` returns `{"status":"started"}` with 200 after Init
- [ ] `/health/readiness` returns `{"status":"ready","checks":{"db":{"status":"ok",...},...}}` with 200 when healthy
- [ ] `/health/readiness` returns 503 when DB connection pool unhealthy
- [ ] `/health/readiness` returns 503 when physics engine not initialized/unhealthy
- [ ] `/health/readiness` returns 503 when MongoDB unreachable
- [ ] `/health/liveness` returns `{"status":"alive",...}` with 200 while engine runs
- [ ] `/health/phase` returns current CleanupPhase string with 200
- [ ] During shutdown, readiness returns 503 (draining)
- [ ] Health responses include per-dependency status JSON with status+message fields
- [ ] `/health` (legacy) delegates to liveness probe (backward compatible)

## Integration Tests

- [ ] Start server → poll `/health/startup` → transitions 503→200 after Init
- [ ] Kill MongoDB → `/health/readiness` transitions to 503
- [ ] Send SIGTERM → `/health/phase` shows progressing cleanup phases
