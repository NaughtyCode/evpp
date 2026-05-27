# P0-6: RunInLoop Lifetime Safety — lua_State Dangling Pointer Prevention

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p0/p0-6-runinloop-safety.md`

## Summary

Added `NetAliveGuard` and `PendingRefTracker` classes to generalize the HTTP
binding's `g_net_alive` + mutex + pending refs pattern. Applied the guard to
all KCP and UDP server `RunInLoop` callbacks that capture raw `lua_State*`
pointers or Lua registry references. Restructured shutdown functions to use
an explicit Shutdown → WaitDrain → direct-cleanup sequence, eliminating the
implicit ordering dependency between `ShutdownNetBindings` and `DestroyScript`.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/script/net_lifetime.h` | `NetAliveGuard` + `PendingRefTracker` classes with TOCTOU-safe acquire/release protocol |
| `src/runtime/script/net_lifetime.cc` | `TryAcquire`/`Release`/`WaitDrain` + `AddRef`/`RemoveRef`/`UnrefAll` implementations |
| `src/tests/unit/vm/test_runinloop_safety.cpp` | 12 tests: guard lifecycle, concurrent acquire, drain blocking, ref tracker |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/script/net_kcp_server_bind.cc` | Added `g_kcp_alive` guard. All 4 RunInLoop sites (message dispatch, cleanup, set_on_message, shutdown) now check `TryAcquire`/`Release`. `ShutdownKcpServerBindings` uses Shutdown→WaitDrain→direct-cleanup instead of RunInLoop-deferred delete. |
| `src/runtime/script/net_udp_server_bind.cc` | Same pattern as KCP: `g_udp_alive` guard, 4 RunInLoop sites protected, `ShutdownUdpServerBindings` uses explicit drain. |
| `src/runtime/CMakeLists.txt` | Added `net_lifetime.cc/.h` to `SCRIPT_SOURCES` |
| `src/tests/unit/CMakeLists.txt` | Added `test_runinloop_safety` target |

## Design

### NetAliveGuard

```
TryAcquire()                    Shutdown() + WaitDrain()
    │                                │
    ├─ fast-path: alive_?            ├─ alive_ = false
    ├─ lock mutex                    ├─ lock mutex
    ├─ re-check alive_               ├─ wait pending_count_ == 0
    ├─ pending_count_++              └─ return (safe to destroy)
    └─ unlock
```

Key invariant: any `TryAcquire` that observes `alive_ == true` (before Shutdown)
will increment `pending_count_` before `WaitDrain` can observe it. `WaitDrain`
blocks until all such callbacks call `Release()`.

### PendingRefTracker

Tracks Lua registry refs held by in-flight callbacks. During shutdown,
`UnrefAll` releases remaining refs under the mutex, preventing TOCTOU races
with callbacks that may still hold refs.

### Shutdown Sequence

```
1. Shutdown()        — no new callbacks can acquire
2. WaitDrain()       — all in-flight callbacks complete
3. Stop servers      — recv threads join
4. Direct cleanup    — unref + delete (no RunInLoop deferral needed)
```

## Acceptance Criteria

- [x] `NetAliveGuard` class implemented and reusable
- [x] `PendingRefTracker` class implemented and reusable
- [x] KCP server binding uses `NetAliveGuard` (4 RunInLoop sites)
- [x] UDP server binding uses `NetAliveGuard` (4 RunInLoop sites)
- [x] Shutdown functions drain in-flight callbacks before releasing resources
- [x] 12 unit tests pass (18 assertions)
- [x] All regression tests pass (sandbox: 115, scriptvm: 40, lual_error: 21, message_limits: 15)
- [x] Build succeeds with no new warnings
