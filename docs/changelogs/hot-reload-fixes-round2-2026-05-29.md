# Hot-Reload System — Comprehensive Fixes (Round 2)

**Date:** 2026-05-29
**Status:** Complete
**Scope:** `src/runtime/vm/script_reloader.*`, `src/runtime/vm/file_watcher.*`, `src/runtime/config/config.*`, `src/runtime/engine/engine.cc`, `resources/script/runtime/common/class.lua`

## Summary

Second-round deep analysis of the hot-reload system identified 8 additional issues beyond Round 1, including a critical data-loss bug (ClearCache wiping all modules), thread-safety gaps, and Lua-side registry fragility. All fixed.

## Issues Fixed

### Critical — Data Loss & Thread Safety

**1. ClearCache wiped all modules, contradicting Round 1 fix #3**
`ReloadFile` at script_reloader.cc:285 called `vm_->GetImporter().ClearCache(L)`, which replaced the entire `package.loaded` table with `{}`, destroying cached state for ALL loaded modules — not just the target module. This directly contradicted the Round 1 changelog claim that "only the target module [is removed] from package.loaded." The targeted removal at lines 272-282 was already correct; the subsequent ClearCache call undid it and destroyed all other modules. Fixed by removing the ClearCache call and the redundant nil-setting block that followed it.

**2. Reload callback invoked on watcher thread for validation failures**
`OnFilesChanged` called `reload_callback_(file, false)` directly on the watcher thread when validation failed (line 114), but `ProcessReloadList` called the same callback on the main thread for reload results. This inconsistent threading could cause crashes if a callback touched non-thread-safe state. Fixed by deferring ALL callback invocations to the main thread: validation failures are queued to a new `pending_failures_` vector and delivered via `RunInLoop` (or `ProcessPendingReloads` for no-EventLoop mode).

**3. MongoDB config accessor data race**
`GetMongoDbDevConfig()` and `GetMongoDbPublicConfig()` returned `const MongoDbConfig&` without holding `config_mutex_`, racing with concurrent `Reload()` writes. Fixed by changing return type to `MongoDbConfig` (by-value, thread-safe copy) and adding `shared_lock` acquisition, matching the pattern of `GetRuntimeConfig()`/`GetClientConfig()`/`GetServerConfig()`.

### High — Functional Correctness

**4. class.lua hot-reload destroys class registry**
The `_registry` table (weak-valued, module-local) was recreated on every reload of `class.lua`, losing all registered class tables. Subsequent `Class("Name")` calls would create new class tables, leaving existing instances' `__index` pointing to stale (potentially GC-eligible) tables. Fixed by storing `_registry` as a well-known global (`__class_registry__`) using `rawget`/`rawset` so it persists across `class.lua` reloads.

**5. ReloadAll non-atomic: partial failure left mixed state**
`ReloadAll` called `ReloadFile` for each file in filesystem order. If file 3/10 failed, files 1-2 had already committed their changes (new code in memory, old code in `package.loaded`), while file 3 was rolled back via its per-file snapshot. The system ended up in a mixed state with inconsistent module versions. Fixed by refactoring `ReloadFile` to extract a `ReloadFileCore` without snapshot/restore, and rewriting `ReloadAll` to snapshot globals once before the loop, reload via `ReloadFileCore`, and restore the initial snapshot on first failure — making the entire operation atomic.

### Medium — Robustness

**6. Manual ReloadFile bypassed debounce tracking**
`ReloadFile` did not update `file_reload_times_`, so a manual reload followed by a watcher-detected change within the debounce window would trigger a redundant second reload. Fixed by recording `steady_clock::now()` in `file_reload_times_` at the end of `ReloadFile`.

**7. FileWatcher quality improvements**
- **Thread naming**: The watcher thread now has a platform-specific name (`evpp_file_watcher`) via `SetThreadDescription` (Windows) / `pthread_setname_np` (Linux) for debugging.
- **WatchDirectory thread safety documented**: Must be called before `Start()`. Not safe to call concurrently with `ScanChanges`.
- **known_files_ type changed**: `unordered_map<string, bool>` replaced with `unordered_set<string>` — same semantics, less memory.

**8. ReloadFile line count and readability**
Extracted `ExtractModuleName` as a free function and `ReloadFileCore` as a private method, reducing `ReloadFile` from ~110 lines to ~35 lines. `ReloadAll` is now a clear two-phase operation: collect files, then atomic reload-or-rollback.

## Files Changed

| File | Change |
|------|--------|
| `src/runtime/vm/script_reloader.h` | Added `ReloadFileCore` declaration, `pending_failures_` vector |
| `src/runtime/vm/script_reloader.cc` | Removed ClearCache call; deferred all callbacks to main thread; extracted `ExtractModuleName` + `ReloadFileCore`; rewrote `ReloadAll` for atomic rollback; added debounce tracking to `ReloadFile` |
| `src/runtime/vm/file_watcher.h` | Changed `known_files_` to `unordered_set`; documented `WatchDirectory` thread safety |
| `src/runtime/vm/file_watcher.cc` | Thread naming; set-based stale cleanup; `<windows.h>` include for `SetThreadDescription` |
| `src/runtime/config/config.h` | Changed `GetMongoDbDevConfig`/`GetMongoDbPublicConfig` to return by value |
| `src/runtime/config/config.cc` | Added `shared_lock` to MongoDB accessor implementations |
| `src/runtime/engine/engine.cc` | Changed `auto&` to `auto` for MongoDB config access (return type changed) |
| `resources/script/runtime/common/class.lua` | Stored `_registry` as global `__class_registry__` to survive class.lua reload |

## Design Decisions

- **ReloadFileCore without snapshot/restore**: Separating the core reload logic from the snapshot wrapper lets `ReloadAll` manage snapshots at its own granularity. Per-file reload (`ReloadFile`) still snapshots independently for its own rollback guarantee.
- **pending_failures_ vector over inline callback**: Storing failed validations alongside pending reloads keeps the main-thread dispatch symmetric. `ProcessPendingReloads` now drains both queues and invokes the callback for each failure before processing valid reloads.
- **Global registry for class.lua**: Using `rawget`/`rawset` on `_G` with a reserved key avoids C++-side changes while ensuring survival. The table remains weak-valued so unreferenced classes are still GC'd.

## Risks

- **class.lua registry as a global**: `__class_registry__` is visible to Lua scripts but uses `rawget`/`rawset` to bypass metatable tricks. A malicious script could delete it, but that requires the `Full` sandbox level (development only). In `Strict`/`Server` levels, the key set of globals is limited.
- **ReloadAll atomicity**: The atomic rollback restores global state but cannot perfectly reverse `package.loaded` changes from previously-succeeded files — those modules' new versions remain cached until a subsequent require or reload cycle. This is an acceptable trade-off: globals consistency is more critical for runtime correctness.
