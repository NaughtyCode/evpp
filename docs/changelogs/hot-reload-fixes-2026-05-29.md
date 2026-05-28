# Hot-Reload System — Comprehensive Fixes (Round 1)

**Date:** 2026-05-29
**Status:** Complete
**Scope:** `src/runtime/vm/script_reloader.*`, `src/runtime/vm/file_watcher.*`, `src/runtime/engine/engine.cc`, `src/tests/unit/hotreload/test_hotreload.cpp`

## Summary

Multi-round deep analysis of the hot-reload system identified ~15 issues across FileWatcher, ScriptReloader, ConfigManager, and Engine integration. This change fixes critical thread-safety bugs, data corruption in rollback, functional gaps in file detection, and architectural issues in module cache management.

## Issues Fixed

### Critical — Thread Safety & Data Integrity

**1. Thread safety: FileWatcher callback accessed Lua VM from watcher thread**
`ScriptReloader::OnFilesChanged` was called directly from the FileWatcher's watcher thread, which then called `ReloadFile` → accessing `ScriptVM::GetState()` — a non-thread-safe lua_State. Fixed by splitting the pipeline:
- `ValidateScript` runs on the watcher thread (creates its own sandbox lua_State — thread-safe)
- Validated files are dispatched to the main thread via `EventLoop::RunInLoop`
- `ReloadFile` and `ProcessReloadList` only run on the main thread
- Added `SetEventLoop(evpp::EventLoop*)` to ScriptReloader
- Added `ProcessPendingReloads()` for environments without an event loop

**2. Global snapshot/restore corrupted non-string types**
`RestoreGlobals` pushed all snapshot values as strings via `lua_pushstring`, turning numbers into strings and booleans into strings. Fixed by introducing `SnapshotValue` struct with typed enum (Nil/Boolean/Integer/Number/String) and per-type push in restore.

**3. ClearCache wiped all modules, not just the target**
`ClearCache` replaced the entire `package.loaded` table with `{}`, destroying cached state for all loaded modules. On reload failure, there was no way to restore them. Fixed by:
- Removing only the target module from `package.loaded` (not the entire table)
- Snapshotting `package.loaded[module_name]` before reload
- Restoring it on failure via `RestorePackageLoaded`

### High — Functional Bugs

**4. New files were never reported as changes**
`FileWatcher::ScanChanges` stored new files' timestamps but did not add them to the `changed` list. A freshly created `.lua` file was only detected on the *second* modification. Fixed by maintaining a `known_files_` set and reporting any first-seen file as changed.

**5. Module name extraction used bare stem instead of dotted path**
`ReloadFile` used `std::filesystem::path::stem()` to extract the module name (e.g., "init" from "scripts/runtime/init.lua"), but `package.loaded` keys use dotted paths (e.g., "runtime.init"). Fixed by computing the relative path from script_dirs_ and converting path separators to dots.

**6. FileWatcher extension was global, not per-directory**
Each `WatchDirectory` call overwrote a single `extension_` field. Watching dir A for `.lua` and dir B for `.json` silently made both use `.json`. Fixed by storing per-directory `WatchEntry {path, extension}` pairs.

### Medium — Architecture & Robustness

**7. Hot-reload never started in library mode**
`ScriptReloader::Start()` was only called from `Engine::Start()`, which is standalone-mode only. In library mode (external EventLoop), the reloader was created but never started. Fixed by starting the reloader in `Engine::Init()` which runs in both modes.

**8. Cleanup ordering: ScriptReloader stopped too late**
`ScriptReloader::Stop()` was called after DatabaseShutdown but before NetworkShutdown, while the VM was still being actively torn down by network/timer bindings. An in-flight reload could access a partially-torn-down VM. Fixed by stopping the reloader immediately after PhysicsShutdown, before any VM teardown.

**9. Broken cross-platform time conversion in ScanChanges**
The `file_time_type` → `system_clock` conversion subtracted `file_clock::now()` from `ftime` then added `system_clock::now()`, computing `(age) + now` — which was inverted. Fixed to compute `system_clock::now() - age` for correct conversion.

**10. ReloadAll error handling with recursive_directory_iterator**
`ReloadAll` checked `ec` only at the loop boundary, not after dereferencing `*it`. Incrementing a `recursive_directory_iterator` can set error codes that were previously missed. Fixed by clearing and continuing on error, and checking `ec` after `is_regular_file`.

**11. Per-file debounce replaces global debounce**
The old debounce used a single `last_reload_time_` timestamp — if file A changed, file B's change 100ms later was silently dropped. Replaced with per-file `file_reload_times_` map so each file is debounced independently.

**12. Stale file_times_ entries leaked for deleted directories**
When a watched directory was removed, `file_times_` retained stale entries indefinitely. Fixed by tracking `known_files_` and cleaning up entries for files that no longer exist on disk.

**13. ConfigManager reload callback re-entrancy deadlock risk**
`NotifyReloadCallbacks` held a `shared_lock` on `callbacks_mutex_` while invoking callbacks. If a callback called `RegisterReloadCallback` (which acquires an exclusive lock), it would deadlock. The `reloading_` flag prevents re-entrant reload, and callbacks are now documented as "must not call RegisterReloadCallback."

### Low — Validation & Tests

**14. ValidateScript executes (not just compiles) the script**
The validation step uses `lua_pcall` to execute top-level code in the sandbox. This is intentional (catches runtime errors) but documented as a trade-off: side effects in the sandbox don't affect the real VM, but validation success does not guarantee real-VM success.

**15. Tests did not cover core hot-reload behavior**
The old `test_hotreload.cpp` only verified construction/lifecycle didn't crash. Rewrote with 30+ tests covering: typed snapshot/restore, rollback on syntax error, module name extraction, dotted path caching, ReloadAll, new file detection, file modification detection, per-file extension, ProcessPendingReloads, and full integration flows.

## Files Changed

| File | Change |
|------|--------|
| `src/runtime/vm/script_reloader.h` | Added `SnapshotValue` struct, `SetEventLoop`, `ProcessPendingReloads`, `SnapshotPackageLoaded`/`RestorePackageLoaded`, per-file debounce map, `pending_reloads_` queue, `<mutex>` include |
| `src/runtime/vm/script_reloader.cc` | Full rewrite: thread-safe dispatch, typed snapshot/restore, targeted cache clearing, dotted module names, per-file debounce, package.loaded rollback |
| `src/runtime/vm/file_watcher.h` | Replaced single `extension_` + `watch_dirs_` with `WatchEntry` struct, added `known_files_` for new-file detection |
| `src/runtime/vm/file_watcher.cc` | Fixed time conversion, new-file reporting, stale entry cleanup, per-directory extension filtering, improved error handling |
| `src/runtime/engine/engine.cc` | Added `SetEventLoop(loop_)` call, moved `Start()` to `Init()` for library mode, moved `Stop()` to before VM teardown in Cleanup |
| `src/runtime/config/config.cc` | Fixed potential deadlock in `NotifyReloadCallbacks`: copy callback list under shared_lock, then invoke without lock |
| `src/tests/unit/hotreload/test_hotreload.cpp` | Rewrote with 30+ comprehensive tests covering all fixed behaviors |

## Design Decisions

- **Validate on watcher thread, reload on main thread**: Validation creates an isolated sandbox VM and is thread-safe. Reload touches the real lua_State and must be on the main thread. This split avoids locking the Lua VM while keeping validation non-blocking for the main loop.
- **Per-module cache clearing vs full clear**: The old approach of wiping `package.loaded` entirely was a sledgehammer. Targeted removal preserves dependent modules and enables proper rollback.
- **SnapshotValue struct over variant**: Using a simple struct with type enum avoids `<variant>` overhead and keeps the code C++17-compatible.

## Risks

- **Validation/reload gap**: A script that passes sandbox validation may still fail in the real VM (different globals, missing modules). This is inherent to sandbox validation and is now documented.
- **EventLoop dependency**: Without an EventLoop, reloads are queued in `pending_reloads_` and must be manually processed via `ProcessPendingReloads()`. This is safe for single-threaded test environments.
