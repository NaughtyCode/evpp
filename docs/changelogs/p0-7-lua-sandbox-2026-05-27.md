# P0-7: Lua Sandbox — Whitelist-Based Library Loading

**Date:** 2026-05-27  
**Status:** Complete  
**Plan:** `./docs/tasks/infra_plan/P0-7-lua-sandbox.md`

## Summary

Replaced `luaL_openlibs` with a configurable sandboxed library loader that
selectively loads Lua standard libraries based on a security level. Three
levels are provided: Strict (production), Server (trusted server scripts),
and Full (development).

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/vm/sandbox.h` | `LuaSandboxLevel` enum + `luaL_openlibs_sandboxed` API |
| `src/runtime/vm/sandbox.cc` | Whitelist-based library loader implementation |
| `src/tests/unit/vm/test_sandbox.cpp` | 16 unit tests covering all sandbox levels |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/vm/vm.h` | Added `LuaSandboxLevel` parameter to `ScriptVM` constructor (default: Full) |
| `src/runtime/vm/vm.cc` | `luaL_openlibs(L_)` replaced with `luaL_openlibs_sandboxed(L_, level)` |
| `src/runtime/config/config.h` | Added `std::string sandbox_level = "strict"` to `RuntimeConfig` |
| `src/runtime/engine/engine.cc` | Parses `runtime_cfg.sandbox_level` string to enum, passes to `ScriptVM` constructor |
| `src/runtime/CMakeLists.txt` | Added `sandbox.cc`/`sandbox.h` to `VM_SOURCES` |
| `src/tests/unit/CMakeLists.txt` | Added `test_sandbox` test target |
| `resources/config/runtime/runtime.json` | Added `"sandbox_level": "strict"` |

## Sandbox Levels

### Strict (production / untrusted scripts)
- Always loaded: base, table, string, math, utf8, coroutine, package
- os: loaded but `execute`, `exit`, `remove`, `rename`, `getenv` removed
- io: not loaded
- debug: not loaded
- `package.loadlib`: always nil

### Server (trusted server-side scripts)
- os: fully available
- io: available
- debug: not loaded
- `package.loadlib`: always nil

### Full (development only)
- All standard libraries loaded
- `package.loadlib`: always nil

## Acceptance Criteria

- [x] `luaL_openlibs_sandboxed(L, LuaSandboxLevel::Strict)` loads only safe libraries
- [x] `luaL_openlibs_sandboxed(L, LuaSandboxLevel::Server)` loads io + os, no debug
- [x] `luaL_openlibs_sandboxed(L, LuaSandboxLevel::Full)` loads all except package.loadlib
- [x] `package.loadlib` is nil at every level
- [x] `ScriptVM` constructor accepts sandbox level (Full by default for backward compat)
- [x] `RuntimeConfig` carries `sandbox_level` string parsed by engine
- [x] 16 unit tests pass across all levels
- [x] Existing `ScriptVM` tests pass with default constructor (Full)
- [x] Build succeeds with no new warnings
