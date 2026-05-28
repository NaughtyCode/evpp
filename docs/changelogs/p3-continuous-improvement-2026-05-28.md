# P3: Continuous Improvement Plans (Multi-DB, CI/CD, Retry, Cleanup)

**Date:** 2026-05-28
**Status:** Complete
**Plans:** `./docs/tasks/infra_plan/p3/`

## Summary

Implemented 8 P3 continuous improvement plans covering database backend
abstraction, CI/CD pipelines, embedded test cleanup, lifecycle documentation,
cursor memory safety, HTTP graceful shutdown, connector retry logic, and
miscellaneous production hardening items.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/database/db_backend.h` | `IDatabaseBackend` interface — CRUD + cursor operations, factory for backend selection (P3-1) |
| `.github/workflows/sanitizers.yml` | ASAN+UBSAN CI workflow on Ubuntu (P3-2) |
| `.github/workflows/lint.yml` | Forbidden patterns check + TODO count enforcement (P3-2) |
| `scripts/pre-commit.sh` | Pre-commit hook for basic lint checks (P3-2) |
| `src/runtime/config/config_validator.h` | `ConfigValidator` with range/value checks (P3-13.3) |
| `src/runtime/config/config_validator.cc` | Validator implementations for port range, thread count, buffer sizes (P3-13.3) |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/engine/engine.cc` | Removed embedded DB smoke test from `Start()`. Added `CleanupPhase` enum with phase tracking in `Cleanup()`. (P3-6, P3-8) |
| `src/runtime/engine/engine.h` | Added `CleanupPhase` enum, `GetCurrentCleanupPhase()`, lifecycle ordering documentation (P3-8) |
| `src/runtime/database/mongo_bind/bind_cursor.cc` | Replaced raw `new`/`delete` of `BsonDocument` with `std::make_unique` in `l_cursor_next` and `l_cursor_current` (P3-10) |
| `src/runtime/evpp/http/http_server.cc` | Replaced TODO markers with 503 Service Unavailable responses when server is stopping (P3-11) |
| `src/runtime/evpp/connector.h` | Added `ConnectorConfig` struct with `max_retries`, `retry_base_ms`, `retry_max_ms`, `retry_exponential_base` (P3-12) |
| `src/runtime/evpp/connector.cc` | Exponential backoff retry logic: `EVUTIL_ERR_CONNECT_RETRIABLE` handling, retry count tracking, state reset on successful connection (P3-12) |
| `src/runtime/evpp/tcp_conn.h` | Added `IsDisconnected()` method (P3-13.1) |
| `src/runtime/evpp/tcp_conn.cc` | Added WARN log on `Send()` when disconnected (P3-13.1) |
| `src/runtime/vm/script_importer.h` | Added `importing_` set for circular import detection (P3-13.2) |
| `src/runtime/vm/script_importer.cc` | Circular import detection via `importing_` set, `ImportStack` formatting, cleanup on all return paths (P3-13.2) |
| `src/runtime/config/config.h` | Added `physics_scene_path` to `RuntimeConfig` (P3-13.4) |

## Implemented Plans

| Plan | Description | Key Change |
|------|-------------|------------|
| P3-1 | Multi-Database Backend Abstraction | `IDatabaseBackend` interface + factory |
| P3-2 | CI/CD Pipeline | Sanitizers workflow, lint workflow, pre-commit hook |
| P3-6 | Embedded Test Cleanup | Removed DB smoke test from `engine.cc Start()` |
| P3-7 | Buffer Cleanup | Already addressed (evppbswap_64 replaced, TODO removed) |
| P3-8 | Cleanup Lifecycle Documentation | `CleanupPhase` enum, phase tracking, ordering doc |
| P3-10 | Cursor Pre-allocation Safety | `std::make_unique` for BsonDocument |
| P3-11 | HTTP Graceful Shutdown | 503 responses when stopping |
| P3-12 | Connector Retry Logic | Exponential backoff with configurable limits |
| P3-13 | Miscellaneous Hardening | Send() failure visibility, circular import detection, config validation, physics scene path |

## Acceptance Criteria

- [x] 8 P3 plans implemented
- [x] 17 files changed across the codebase
- [x] All new features compile and pass existing regression tests
- [x] Build succeeds with no new warnings
