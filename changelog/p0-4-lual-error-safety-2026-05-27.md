# P0-4: luaL_error Exception Safety — Prevent C++ Destructor Bypass

**Date:** 2026-05-27
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p0/p0-4-lual-error-safety.md`

## Summary

`luaL_error` uses `longjmp` which unwinds the C stack without calling C++
destructors. Fixed the one active leak site (`l_net_server_listen`) where
`std::string name` was alive on the stack during `luaL_error` calls. Created
a documented `LuaError` helper to make future error sites auditable.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/script/bind_util.h` | `LuaError()` helper — documented, variadic, pushes formatted error then calls `lua_error` |
| `src/runtime/script/bind_util.cc` | Implementation with 512-byte stack buffer via `vsnprintf` |
| `src/tests/unit/vm/test_lual_error_safety.cpp` | 6 tests: helper formatting, scope-block pattern, ScriptVM survival after longjmp |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/script/net_tcp_server_bind.cc` | Inlined `std::string name` construction into `TCPServer` constructor — temporary destroyed before Init/Start error paths |
| `src/runtime/CMakeLists.txt` | Added `bind_util.cc`/`.h` to `SCRIPT_SOURCES` |
| `src/tests/unit/CMakeLists.txt` | Added `test_lual_error_safety` target |

## Risk Audit Results

An audit of all `luaL_error` call sites across 8 binding files (~90 call sites) found:

- **1 active leak** (fixed): `l_net_server_listen` — `std::string name` alive at Init/Start error paths
- **~89 safe calls**: simple pointer/flag validation with no RAII objects on stack — safe as-is

## Technical Approach

The scope-block pattern: wrap RAII objects in an inner `{}` block that exits
before any `luaL_error`/`lua_error` call. Destructors run at the closing
brace, guaranteed by the C++ standard — `longjmp` cannot skip them when they
have already been destroyed.

In this case the fix is even simpler: construct the temporary `std::string`
inline so it is destroyed at the full-expression semicolon, before error checks.

## Acceptance Criteria

- [x] `LuaError()` helper available in `bind_util.h`
- [x] `l_net_server_listen` has no RAII objects alive at `luaL_error` calls
- [x] All existing tests continue to pass (sandbox: 115 assertions, scriptvm: 40 assertions)
- [x] 6 new unit tests pass (21 assertions)
- [x] Build succeeds with no new warnings
