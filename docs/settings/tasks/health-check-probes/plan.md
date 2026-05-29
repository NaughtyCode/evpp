# Implementation Plan: Health Check Probes

## Step 1: Add three health endpoints

Modify `admin_http.cc`:
- `/health/startup` — returns 503 until `Engine::Init()` completes, then 200
- `/health/readiness` — checks DB, physics, MongoDB; returns 200 only if all healthy
- `/health/liveness` — lightweight: checks event loop alive; returns 200 quickly
- Keep `/health` as legacy endpoint (maps to liveness for backward compat)

## Step 2: Track initialization state

Modify `engine.h/cc`:
- Add `std::atomic<bool> initialized_{false}` set at end of `Init()`
- Add `std::atomic<CleanupPhase> current_phase_{CleanupPhase::None}`
- Startup probe returns 200 when `initialized_` is true

## Step 3: Add dependency health checks

Modify `admin_http.cc`:
- DB health: call `DatabaseService::Instance().IsHealthy()`
- Physics health: check `PhysicsEngineBridge::Instance().IsInitialized()`
- MongoDB health: verify `IsMongoDbDevLoaded() || IsMongoDbPublicLoaded()`
- Return JSON with per-dependency status: `{"db": "ok", "physics": "ok", "mongodb": "degraded"}`

## Step 4: Expose CleanupPhase

Modify `admin_http.cc`:
- Add `/health/phase` endpoint returning current `CleanupPhase` as string
- Add `cleanup_phase` field to all health responses during shutdown
- Load balancer can use this for graceful drain

## Step 5: Structured error codes

Modify all health endpoints:
- Return `{"status": "unhealthy", "checks": {"db": {"status": "error", "message": "..."}}}`
- HTTP status: 200 for healthy, 503 for unhealthy, 500 for internal error

## Step 6: Update tests

In new `test_health.cpp`:
- Test: startup probe returns 503 before Init completes
- Test: startup probe returns 200 after Init
- Test: readiness probe fails when DB unhealthy
- Test: liveness probe always returns 200 while event loop runs
- Test: cleanup phase exposed during shutdown
