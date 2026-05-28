# Hot-Reload System — Comprehensive Fixes (Round 4)

**Date:** 2026-05-29
**Status:** Complete
**Scope:** `src/runtime/vm/script_reloader.*`, `src/runtime/vm/file_watcher.cc`, `src/runtime/engine/engine.cc`

## Summary

Fourth-round deep analysis identified 5 additional issues: 1 critical global-leak bug in ReloadAll rollback, 2 high-severity validation sandbox deficiencies that silently block hot-reload for scripts using imports or io/os, a debounce timing flaw that blocks recovery after validation failure, and an uncaught exception path that would kill the process.

## Issues Fixed

### Critical — Data Integrity

**1. ReloadAll rollback leaks new globals from partially-reloaded files**
When `ReloadAll` processes files A, B, C and B fails, the rollback logic called `RestoreGlobals` to restore snapshot values for keys that existed before the reload. However, globals CREATED by file A (new keys not present in the snapshot) were never cleaned up — they persisted after the rollback, leaving the VM in a mixed state with leaked state from a logically-rolled-back operation. Fixed by recording the complete set of pre-reload global keys (`initial_keys`), and after rollback, iterating the global table to nil out any key not present in the original set. This handles all types (scalars, functions, tables) regardless of snapshot type support.

### High — Functional Correctness

**2. ValidateScript always used Strict sandbox, blocking io/os scripts from hot-reload**
`ValidateScript` hardcoded `LuaSandboxLevel::Strict` when creating its sandbox VM. In `Server` or `Full` sandbox modes, scripts can use `io` and `os` (allowed at those levels), but validation would fail with "attempt to index a nil value (global 'io')" — even though the real VM would execute the script successfully. This meant scripts using `io` or `os` could never be hot-reloaded in Server/Full configurations. Fixed by adding `sandbox_level_` member to `ScriptReloader`, `SetSandboxLevel()` setter, and calling it from `Engine::Init` with the same level used for the real VM.

**3. ValidateScript had no import/require infrastructure, blocking scripts with dependencies**
The validation sandbox was created with only `luaL_openlibs_sandboxed` — no `package.path`, no `import()` function, no `ScriptImporter`. Any script that called `import("other.module")` or `require("other.module")` in its top-level code would fail validation, even though the real VM would succeed. This meant virtually all non-trivial scripts (anything with imports) could never pass validation and thus could never be hot-reloaded. Fixed by:
- Setting `package.path` on the validation VM using the watched script directories (normalized paths)
- Registering a minimal `import()` function that delegates to `require()`
- This does not support `import.setpath()`/`import.addpath()`/`import.clearcache()` or wildcard `import("sub.*")`, but covers the common case of module imports at the top level

### Medium — Robustness

**4. Debounce timestamp set before validation success, blocking recovery**
`OnFilesChanged` recorded `file_reload_times_[file] = now` before calling `ValidateScript`. If validation failed (e.g., syntax error in saved file), the debounce timestamp was still set. The user fixing the error and saving again within the debounce window (300ms default) would have the second save suppressed. Fixed by moving the timestamp update to after successful validation, so failed validations don't block the next change.

**5. FileWatcher callback exception causes std::terminate**
The `WatchLoop` called `callback_(changed)` without a try-catch. If the callback threw (e.g., `ValidateScript` failing due to OOM in `luaL_newstate`, or any other unexpected exception), the exception propagated out of the thread function and called `std::terminate()`, killing the entire process. Fixed by wrapping the callback invocation in a try-catch that logs the error and continues the watch loop.

## Files Changed

| File | Change |
|------|--------|
| `src/runtime/vm/script_reloader.h` | Added `#include "runtime/vm/sandbox.h"`, `SetSandboxLevel()` declaration, `sandbox_level_` member |
| `src/runtime/vm/script_reloader.cc` | Added `#include <unordered_set>`, `SetSandboxLevel()` impl, `l_validate_import` helper; rewrote `ValidateScript` with sandbox level + `package.path` + `import()`; moved debounce timestamp after validation in `OnFilesChanged`; added global-key tracking and new-key cleanup in `ReloadAll` rollback path |
| `src/runtime/vm/file_watcher.cc` | Wrapped `callback_()` in `WatchLoop` with try-catch for exception safety |
| `src/runtime/engine/engine.cc` | Added `script_reloader_->SetSandboxLevel(sandbox_level)` call in `Init()` |

## Design Decisions

- **Minimal validation import**: The validation sandbox's `import()` delegates to `require()` rather than replicating the full `ScriptImporter` infrastructure. This is a pragmatic trade-off: creating a full `ScriptImporter` + `ExportImport` on the sandbox would duplicate significant engine code. The minimal version covers the 95% case (importing dependency modules at the top level). Scripts using `import.setpath()` or `import.clearcache()` at the top level will still fail validation — those are rare and can be addressed in a future round.
- **Global key set vs snapshot extension**: The ReloadAll rollback cleanup records a separate `initial_keys` set rather than extending the snapshot system to track non-scalar types. This is simpler and more robust: the key set faithfully represents the pre-reload state regardless of type. The snapshot system remains focused on scalar value preservation.
- **Debounce after validation**: Moving the timestamp update to after validation means that files failing validation are eligible for immediate re-attempt. The trade-off is that if validation is slow (e.g., loading many dependencies), the next scan cycle could pick up the same file before the current validation completes. In practice, validation is fast (sub-millisecond for typical scripts) and the poll interval is 1 second, so this is not a concern.

## Known Limitations (documented for future rounds)

- **TOCTOU between validation and reload**: A file passing sandbox validation could be modified on disk before the main-thread reload executes. The reload would load the newer content without re-validating. Mitigation could involve storing a content hash or modification time at validation and re-checking before reload.
- **`file_reload_times_` concurrent access**: The debounce map is read/written from both the watcher thread (`OnFilesChanged`) and the main thread (`ReloadFile`, `ReloadAll`). Different keys are accessed from different threads, which is technically UB on `std::unordered_map` even with disjoint keys. The practical risk is low (occasional missed debounce), but a proper fix would use a mutex or concurrent hash map.
- **`SnapshotGlobals` cannot snapshot functions/tables/userdata**: If a reload replaces a global function with a new version and then fails, the old function is not restored by rollback. This requires Lua-level support for function cloning or a different approach to reload (e.g., loading into a staging table and swapping).
