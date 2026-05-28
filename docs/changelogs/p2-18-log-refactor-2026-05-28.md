# P2-18: Replace fprintf/cout with ENGINE_LOG in Runtime Subsystems

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-18-log-refactor.md`

## Summary

Converted 31 `fprintf(stderr, ...)` and 16 `std::cout` calls to `ENGINE_LOG_*`
macros across 6 subsystems: database service, MongoDB helpers, event watcher,
physics assets, and timer manager. 51 `fprintf` calls remain in pre-logger boot
paths (config, engine init, event loop creation) where Quill is not yet available.
Zero `std::cout` calls remain in runtime.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/core/timer/timer_manager.cc` | Replaced 16 `std::cout` calls with `ENGINE_LOG_DEBUG`/`ENGINE_LOG_TRACE` |
| `src/runtime/database/data_service/database_service.cc` | Replaced `fprintf(stderr, ...)` with `ENGINE_LOG_WARN` |
| `src/runtime/database/mongo/mongo_oidc.cc` | Replaced `fprintf(stderr, ...)` with `ENGINE_LOG_ERROR` |
| `src/runtime/database/mongo/mongo_session.cc` | Replaced `fprintf(stderr, ...)` with `ENGINE_LOG_WARN` |
| `src/runtime/evpp/event_watcher.cc` | Replaced `fprintf(stderr, ...)` with `ENGINE_LOG_ERROR`. Also fixed 7 corrupted format strings and added missing `engine::` prefix on `GetLogger()` calls. |
| `src/runtime/physics/physics_assets.cc` | Replaced `std::cout <<` debug output with `ENGINE_LOG_DEBUG` |

## Design Decisions

- **Pre-logger paths preserved**: Config parsing, engine initialization, and event loop creation run before `InitLogger()`. Their `fprintf` calls were intentionally kept — Quill is not yet available at those points.
- **Severity mapping**: `fprintf(stderr, ...)` → `ENGINE_LOG_ERROR`/`ENGINE_LOG_WARN`; `std::cout <<` → `ENGINE_LOG_DEBUG`/`ENGINE_LOG_TRACE` based on content (debug output vs. error reporting).

## Acceptance Criteria

- [x] 31 `fprintf` calls replaced with `ENGINE_LOG_*`
- [x] 16 `std::cout` calls replaced — zero remain in runtime
- [x] 51 `fprintf` calls intentionally kept in pre-logger boot paths
- [x] 7 corrupted format strings in `event_watcher.cc` repaired
- [x] Build succeeds with no new warnings
