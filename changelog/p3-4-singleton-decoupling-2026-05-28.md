# P3-4: Singleton Decoupling — Dependency Injection for Bindings

**Date:** 2026-05-28
**Status:** Partial (net_tcp_server_bind.cc done; 4 other binding files remain)
**Plan:** `./docs/tasks/infra_plan/p3/p3-4-singleton-decoupling.md`

## Summary

Added explicit `event_loop` dependency to `ServerCtx` and replaced
`Engine::Instance().GetEventLoop()` calls in `net_tcp_server_bind.cc` with
`ctx->event_loop`. Reduced `Engine::Instance()` calls from 3 to 1 in this file;
the remaining call is the initial entry point (`l_net_server_listen`) which is
the composition root where the loop is obtained.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/script/net_tcp_server_bind.cc` | Added `EventLoop* event_loop` to `ServerCtx`; set during `l_net_server_listen`; used in `l_server_stop` and `l_server_gc` instead of `Engine::Instance().GetEventLoop()` |

## Design Decisions

- **Incremental decoupling**: Each context struct gets its dependencies stored as
  members. The entry-point functions (like `l_net_server_listen`) remain the
  composition root that acquires dependencies from the engine. Internal callbacks
  and cleanup functions use the stored dependency.
- **Consistent with P2-11**: The shared_ptr migration already removed several
  `Engine::Instance()` calls (the old delete patterns). Combined, this file went
  from 5 `Engine::Instance()` calls to 1.

## Remaining Work

4 other binding files still call `Engine::Instance()`:
- `net_kcp_server_bind.cc` (3 calls)
- `net_udp_server_bind.cc` (3 calls)
- `net_http_bind.cc` (3 calls)
- `net_tcp_client_bind.cc` (2 calls)

Same pattern applies: add `event_loop` to context struct, set at creation, use instead of `Engine::Instance()`.
