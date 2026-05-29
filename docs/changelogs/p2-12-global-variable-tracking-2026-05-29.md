# P2-12: Global Variable Ownership Tracking

**Date:** 2026-05-29
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-12-global-tracking.md`

## Summary

Added per-module global variable ownership tracking to `ScriptImporter`. Before each
module execution, the global table _G is snapshotted; after execution, new global keys
are attributed to the module and recorded. `ClearCache()` now nils all tracked globals
before resetting `package.loaded`, preventing stale global state from surviving hot-reload.
A warning is logged when modules set globals, encouraging the local-table-return pattern.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/vm/script_importer.h` | Added `ModuleGlobals` private struct, `module_globals_` map, `SnapshotGlobalKeys()` static helper, `TrackNewGlobals()` method |
| `src/runtime/vm/script_importer.cc` | Implemented `SnapshotGlobalKeys` (iterate _G), `TrackNewGlobals` (diff before/after, warn, record), wired into `ImportSingle` and `ImportAll`, updated `ClearCache` to nil tracked globals before clearing `package.loaded` |

## Design Decisions

- **Per-importer state**: `module_globals_` is an instance member, not a static global.
  Each VM has its own `ScriptImporter` and its own tracking, so hot-reload on one VM
  does not interfere with another.
- **Warn on every global**: No config toggle — the warning is always logged at WARN
  level. It fires at development time and costs nothing in production to keep.
- **Snapshot is O(n)**: Iterating the global table keys is cheap for typical module
  counts. The snapshot only covers string keys to skip internal numeric-indexed entries.

## Acceptance Criteria

- [x] Module global variable ownership tracked in `module_globals_`
- [x] `ClearCache()` nils all tracked globals before clearing `package.loaded`
- [x] `ImportSingle` and `ImportAll` both track new globals
- [x] Warning logged when modules set globals
- [x] `ImportAll` `cache_name` avoids duplicate `std::string` declaration
