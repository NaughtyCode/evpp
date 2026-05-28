# Lua Error Dispatch Unification — traceback on all user-facing pcall paths

**Date**: 2026-05-29
**Scope**: 15 files across `src/runtime/` (script, vm, space, physics, database)

## Summary

Unified all Lua error dispatch paths to include stack traceback via
`PushLuaErrorHandler` / `PushLuaErrorHandlerForCall`. Previously, all 19
user-facing `lua_pcall(..., 0)` calls used `msgh=0` (no error handler),
meaning Lua runtime errors produced only the error message with zero
context about where in the call stack the error originated.

## Changes

### New helper: `PushLuaErrorHandlerForCall`

Added to `lua_error_handler.h`. Computes the function's stack position,
pushes the traceback error handler, and inserts it below the function.
Returns the handler's stack index for use as `msgh` in `lua_pcall`.

### Updated helpers: `bind_util.h`

`CallInstMethod`, `CallInstMethodStr`, `CallInstMethodTableStr` now use
`PushLuaErrorHandler` + `lua_pcall` with traceback. These three helpers
cover all TCP/KCP/UDP callback dispatch (on_connect, on_message, on_close).

### Updated call sites (13 files)

| File | nargs | Context |
|------|-------|---------|
| `vm.cc` | 0 | `CallGlobalFunction` (InitScript/UpdateScript/DestroyScript) |
| `vm.cc` | 0 | `DoFile` / `DoString` |
| `net_http_bind.cc` | 2 | HTTP response callback |
| `timer_bind.cc` | 0 | Timer callback |
| `entity_bind.cc` | 2 | Entity send via conn |
| `entity_bind.cc` | 0 | Entity timer callback |
| `aoi_bind.cc` | 3 | AOI enter/leave event |
| `rpc_bind.cc` | 4 | RPC client send callback |
| `rpc_bind.cc` | 2 | RPC client async callback |
| `physics_system.cc` | 1 | on_physics_collision |
| `space_message.cc` | 4 | Space message delivery |
| `connection_router.cc` | 2 | Connection data routing |
| `db_script_vm.cc` | 1 | on_db_frame callback |
| `net_kcp_server_bind.cc` | 3 | KCP on_message |
| `net_udp_server_bind.cc` | 2 | UDP on_message |
| `script_reloader.cc` | 0 | Script validation |

### Intentionally skipped (non-user-callback paths, 4 calls)

- `script_importer.cc` (×2) — `require()` module loader, system path
- `msgpack_bind.cc` — msgpack unpack with `LUA_MULTRET`, serialization
- `rpc_bind.cc` (RPC server handler) — complex result-returning nresults=1 handler

## Impact

- **Before**: `[net.tcp_server] on_message error: attempt to index a nil value`
- **After**: `[net.tcp_server] on_message error: attempt to index a nil value\nstack traceback:\n\t[C]: in function 'error'\n\tserver.lua:42: in method 'on_message'`
- Error messages now include the exact Lua file and line number
- Zero API changes — all changes are internal to the C++ binding layer
