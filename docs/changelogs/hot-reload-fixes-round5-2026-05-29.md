# Hot-Reload System — Comprehensive Fixes (Round 5)

**Date:** 2026-05-29
**Status:** Complete
**Scope:** `src/runtime/vm/script_reloader.*`

## Summary

Fifth-round analysis focused on thread safety, resource management, and rollback completeness. Identified 3 issues: a data race on `file_reload_times_` between watcher and main threads, inability to roll back non-scalar globals (functions/tables/userdata), and incomplete `package.loaded` restoration in `ReloadAll` rollback. All fixed.

## Issues Fixed

### High — Thread Safety

**1. Data race on `file_reload_times_` between watcher and main threads**
`file_reload_times_` (the per-file debounce map) was read and written from both the watcher thread (`OnFilesChanged`) and the main thread (`ReloadFile`, `ReloadAll`) without any synchronization. While the practical risk was low (different keys accessed from different threads), this is undefined behavior per the C++ standard for `std::unordered_map`. Fixed by adding `std::mutex file_reload_mutex_` and guarding all access points:
- `OnFilesChanged` debounce check and timestamp writes
- `ReloadFile` timestamp write
- `ReloadAll` timestamp writes
- `Start` clear

### High — Functional Correctness

**2. SnapshotGlobal silently dropped functions, tables, and userdata**
The `SnapshotValue` system only supported scalar types (Nil, Boolean, Integer, Number, String). When a script defined a global function or table and the reload failed, rollback could not restore the old value — the old function/table was lost. This was the most impactful remaining gap in rollback correctness, since scripts commonly define global functions. Fixed by:
- Adding `Reference` type to `SnapshotValue::Type`, using `luaL_ref` to store registry references
- `SnapshotGlobal` now pushes a copy of the value and stores it via `luaL_ref` for all non-scalar types
- `RestoreGlobals` pushes referenced values back via `lua_rawgeti`
- Added `ClearSnapshot(L)` helper that properly calls `luaL_unref` on all Reference entries before clearing the map
- `SnapshotGlobals` uses `ClearSnapshot` instead of raw `clear()`
- `Start()` uses `ClearSnapshot` to prevent registry leaks on restart

This also fixed a latent resource leak: when `SnapshotGlobals` was called a second time, old Reference entries were silently dropped without calling `luaL_unref`, leaking registry slots. `ClearSnapshot` eliminates this leak.

### Medium — Rollback Completeness

**3. ReloadAll did not restore `package.loaded` for previously-succeeded files**
When `ReloadAll` rolled back after a mid-loop failure (e.g., file 3 of 10 fails), only the failing file's `package.loaded` entry was restored via `RestorePackageLoaded`. Files 1–2 had their new module versions cached in `package.loaded`, producing an inconsistent state where some modules used new code and others used rolled-back code. Fixed by snapshotting the entire `package.loaded` table before the reload loop (via `luaL_ref`) and restoring it in full on rollback, ensuring all module caches revert to their pre-reload state.

**Bonus: Resource cleanup on ReloadAll success path**. The `initial_snapshot` (moved away from `global_snapshot_`) and the `pkg_loaded_snapshot` reference were not cleaned up on the success return path, leaking registry references. Now properly unreffed on both success and failure paths.

## Files Changed

| File | Change |
|------|--------|
| `src/runtime/vm/script_reloader.h` | Added `Reference` to `SnapshotValue::Type` enum, `ref_val` field; added `ClearSnapshot()` declaration; added `file_reload_mutex_` member |
| `src/runtime/vm/script_reloader.cc` | Implemented `ClearSnapshot()`; updated `SnapshotGlobal` to store references for non-scalars; updated `RestoreGlobals` to restore references; updated `SnapshotGlobals` to use `ClearSnapshot`; guarded all `file_reload_times_` accesses with mutex; added full `package.loaded` snapshot/restore in `ReloadAll`; added resource cleanup on ReloadAll success path |

## Design Decisions

- **Registry references over deep copy**: Using `luaL_ref`/`lua_rawgeti` to store references rather than attempting deep copies of tables/functions. This correctly preserves function identity and avoids the impossibility of deep-copying Lua closures with upvalues. Tables stored as references will reflect any mutations that occurred between snapshot and restore — this is a known limitation, but the alternative (no rollback at all for table globals) is worse.
- **Per-access mutex locking over coarse-grained lock**: The `file_reload_mutex_` is locked at each individual access to `file_reload_times_` rather than held across the entire `OnFilesChanged` or `ReloadAll` method. This avoids holding the mutex during expensive operations like validation or file I/O, keeping the critical section minimal.
- **Full `package.loaded` snapshot vs per-entry tracking**: Snapshotting and restoring the entire `package.loaded` table is simpler than tracking individual module entries. Since `ReloadAll` runs on the main thread with exclusive VM access, replacing the entire table is safe and atomic from the perspective of other engine code.
