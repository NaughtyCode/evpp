# P2-11: RunInLoop shared_ptr Migration

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-11-runinloop-smart-ptr.md`

## Summary

Eliminated all 10 `RunInLoop([del_ctx]{ delete del_ctx; })` / `QueueInLoop([del_ctx]{ delete del_ctx; })`
delayed-delete patterns across `net_tcp_server_bind.cc` and `net_tcp_client_bind.cc`.
Replaced with `std::shared_ptr` ownership via `g_conn_shared` / `g_server_shared` /
`g_client_shared` maps. Context lifetime is now deterministic: the context is
destroyed when the last `shared_ptr` reference is released, with no dependency
on the event loop being alive.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/script/bind_util.h` | Added `SharedCtx<T>` userdata holder, `SharedCtxGC`, `PushInstanceTableShared` for shared_ptr-based Lua userdata |
| `src/runtime/script/net_tcp_server_bind.cc` | Added `g_conn_shared`/`g_server_shared` maps; `new ConnCtx()`/`new ServerCtx()` → `make_shared`; 6 `RunInLoop delete` patterns → `shared_ptr` map `erase()` |
| `src/runtime/script/net_tcp_client_bind.cc` | Added `g_client_shared` map; `new ClientCtx()` → `make_shared`; 4 `RunInLoop delete` patterns → `shared_ptr` map `erase()` |

## Design Decisions

- **Minimal API disruption**: `GetCtxFromTable<T>` still returns raw pointer.
  All existing binding code continues to work unchanged. Only the ownership
  tracking layer changed.
- **No RunInLoop dependency**: The original code relied on the event loop being
  alive to process the delete callback. During shutdown, a stopped event loop
  would leak contexts. shared_ptr erasure is immediate and loop-independent.
- **QueueInLoop preserved for ServerCtx**: ServerCtx erasure is still queued
  to allow `HandleClose` (conn cleanup) to fire first, preserving ordering.

## Acceptance Criteria

- [x] All 10 `RunInLoop([del_ctx]{ delete del_ctx; })` patterns eliminated
- [x] Context lifetime managed by `shared_ptr` maps
- [x] `GetCtxFromTable` API unchanged
- [x] ServerCtx cleanup ordering preserved (QueueInLoop)
- [x] Error paths (`server init failed`/`start failed`) properly erase from map
