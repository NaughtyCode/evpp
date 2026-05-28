# P2-8: evpp Release Build Thread Safety Checks

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-8-evpp-release-safety.md`

## Summary

Extended thread safety checks for libevent `event_add`/`event_del` from
`#ifdef H_DEBUG_MODE` only to all build modes. Debug builds retain the full
map-based duplicate/cross-thread checking. Release builds now include a
lightweight thread-local check that logs ERROR when `event_add`/`event_del`
is called from a non-owning thread.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/inner_pre.h` | Added `SetTlsEventBase()`, `ClearTlsEventBase()`, `GetTlsEventBase()` declarations |
| `src/runtime/evpp/inner_pre.cc` | Added `thread_local tls_event_base`; `#else` branch in `EventAdd`/`EventDel` with lightweight `event_get_base()` cross-thread check |
| `src/runtime/evpp/event_loop.cc` | `EventLoop::Run()` sets `tls_event_base` before `event_base_dispatch`, clears after |

## Design Decisions

- **Two-tier checking**: Debug → full `std::map`-based duplicate/cross-thread
  checking. Release → lightweight `thread_local` pointer comparison (zero
  allocations, no mutex).
- **`thread_local` overhead**: One pointer read + one compare per `EventAdd`/
  `EventDel` call in Release mode. Negligible performance impact.
- **Log-only in Release**: Wrong-thread calls are logged at ERROR level but
  not `assert`-ed. Debug builds still `assert(false)`.

## Acceptance Criteria

- [x] Release builds include lightweight cross-thread check
- [x] Wrong-thread calls logged at ERROR level
- [x] Debug builds retain full map-based checking
- [x] `tls_event_base` set/cleared at `EventLoop::Run()` boundaries
