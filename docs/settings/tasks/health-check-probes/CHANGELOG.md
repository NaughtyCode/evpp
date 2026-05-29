# Changelog: Health Check Probes

## 2026-05-29 — Initial implementation

### Added
- `/health/startup` endpoint — returns 200 only after full initialization (`Engine::initialized_`)
- `/health/readiness` endpoint — validates all backend dependencies (DB, MongoDB, Physics)
- `/health/liveness` endpoint — lightweight event loop + running check
- `/health/phase` endpoint — exposes CleanupPhase for load balancer drain coordination
- `std::atomic<bool> initialized_` in Engine, set at end of `Init()`
- `std::atomic<CleanupPhase> cleanup_phase_` in Engine (was non-atomic)
- `Engine::initialized()` and `Engine::cleanup_phase()` public accessors
- `PhysicsEngineBridge::IsInitialized()` delegating to `PhysicsSystem::IsInitialized()`
- Per-dependency health status with nested JSON: `{"db": {"status":"ok","message":""}, ...}`
- Structured error codes with status + message per dependency
- Unit tests in `test_health.cpp` covering Engine state, PhysicsEngineBridge, AdminHttpServer

### Changed
- `/health` legacy endpoint now delegates to liveness probe (backward compatible JSON format)
- `Engine::cleanup_phase_` converted from plain enum to `std::atomic<CleanupPhase>` for thread-safe reads from health handlers

### Fixed
- P0-6: Health check now validates backend dependencies (DB, MongoDB, Physics)
- P3-8: CleanupPhase now externally visible via `/health/phase` and `cleanup_phase` field in all responses
- P3-9: Startup/Readiness/Liveness probes now differentiated
