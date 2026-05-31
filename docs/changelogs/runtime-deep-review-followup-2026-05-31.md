# Runtime Deep Review Follow-up - 2026-05-31

## Context

Follow-up implementation for the remaining practical items in `docs/analysis/runtime/runtime_deep_review_2026-05-31.md`, after the initial runtime reliability and observability pass.

## Admin HTTP Hardening

- Added `server.admin_auth_token` and `server.admin_metrics_enabled`.
- Admin HTTP now rejects non-loopback binds unless a bearer token is configured.
- Admin handlers now support `Authorization: Bearer <token>` and `X-Admin-Token`.
- `/metrics` can be disabled through config and returns 404 when disabled.
- Config reload diff reports admin token state as `set`/`unset` instead of exposing the secret value.

## Space And Entity Timers

- Space-created entities now receive the active `TimerManager`.
- Entity timers now support an owner resolver, so space-local entities are looked up through their owning `Space` instead of the global `EntityManager`.
- Added coverage proving a timer owned by a space entity fires through space-local lookup.

## Database And ORM Backpressure

- ORM write enqueue failures are no longer silent: failed async persistence requests are retained in a pending retry list.
- Added `PendingPersistenceFailureCount`, `RetryPendingPersistenceFailures`, and `ClearPendingPersistenceFailures`.
- DB thread response overflow no longer drains and discards old completed responses to make room for newer ones; it uses a bounded overflow buffer first.

## Shutdown And Allocation Safety

- Engine cleanup timeout no longer immediately calls `std::quick_exit`; it logs a critical timeout and continues best-effort teardown so logs, profiler, VM, timers, DB, and network cleanup still have a chance to flush.
- `SpaceManager::CreateSpace` now has a bounded allocation attempt limit and logs critical failure instead of looping forever.
- `CreateSpaceWithId(UINT64_MAX)` no longer wraps the next automatic allocator back to zero.

## Tests

- Added config validation tests for public admin bind with and without token.
- Added Admin HTTP lifecycle coverage for refusing unauthenticated non-loopback bind.
- Added ORM persistence backpressure visibility coverage.
- Added space entity timer coverage.

## Verification

- `cmake --build artifacts\build --config Debug --target test_config test_health test_space test_orm test_data_service`
- `ctest --test-dir artifacts\build -C Debug --output-on-failure -R 'unit\.(config|health|space|database\.(orm|data_service))'`
- `cmake --build artifacts\build --config Debug`
- `ctest --test-dir artifacts\build -C Debug --output-on-failure`
- Result: 52/52 tests passed.
