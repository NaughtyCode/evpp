# Hot-Reload System — Comprehensive Fixes (Round 3)

**Date:** 2026-05-29
**Status:** Complete
**Scope:** `src/runtime/vm/file_watcher.*`, `src/runtime/vm/script_reloader.cc`

## Summary

Third-round deep analysis identified 5 remaining issues: a functional bug where the first watcher scan triggers unnecessary mass reload of all scripts at startup, zombie state on thread creation failure, stale state on re-start, missing debounce tracking in ReloadAll, and a potential time overflow in clock conversion.

## Issues Fixed

### High — Functional Correctness

**1. First watcher scan reported all existing files as "new," triggering mass reload**
After `FileWatcher::Start()`, the first `ScanChanges()` (after `poll_interval_ms`) found no files in `known_files_`, so every existing `.lua` file was reported as a change. The `ScriptReloader` then validated and reloaded ALL scripts ~1 second into runtime — unnecessary, potentially slow, and risky if scripts depend on initialization state from `InitScript()`. Fixed by adding `FileWatcher::PrimeKnownFiles()`, which pre-populates `known_files_` and `file_times_` by scanning watched directories without invoking the change callback. `ScriptReloader::Start()` calls this before starting the watcher thread, so the first real scan only reports genuine post-startup changes.

### Medium — Robustness

**2. ScriptReloader::Start() did not reset state on re-start**
Calling `Start()` a second time created a new `FileWatcher` (destroying the old one and joining its thread), but `file_reload_times_`, `global_snapshot_`, and both pending queues retained stale data from the previous run. Stale debounce timestamps could suppress legitimate reloads; stale pending entries could cause spurious reloads. Fixed by explicitly clearing all mutable state at the top of `Start()` before creating the new watcher.

**3. FileWatcher::Start() set `running_ = true` before thread creation**
If `std::make_unique<std::thread>` threw `std::system_error` (extremely rare, but possible under resource exhaustion), `running_` remained `true` with no backing thread. `Stop()` would check `running_`, find it true, skip the nullptr `thread_`, and leave `running_` permanently true — a zombie watcher. Fixed by moving `running_.store(true)` to after successful thread creation.

**4. ReloadAll did not update debounce timestamps**
`ReloadAll` calls `ReloadFileCore` directly (not `ReloadFile`), so `file_reload_times_` was never updated. If the file watcher detected a change to a file shortly after `ReloadAll`, it would reload it again within the debounce window. Fixed by recording `steady_clock::now()` in `file_reload_times_` for each file processed by `ReloadAll` (both success and failure paths).

### Low — Edge Case Safety

**5. ToSystemClock could produce far-future timestamps**
If a file's recorded modification time was in the future relative to `file_clock::now()` (e.g., clock skew, VM snapshot restore), `age = file_now - ftime` was negative. Then `sys_now - negative_duration = sys_now + |age|` mapped the file's time far into the future, causing `sctp > it_mtime->second` to be true on every scan (perpetual "changed" for that file). Fixed by clamping `age` to zero when negative.

## Files Changed

| File | Change |
|------|--------|
| `src/runtime/vm/file_watcher.h` | Added `PrimeKnownFiles()` declaration |
| `src/runtime/vm/file_watcher.cc` | Fixed `running_` ordering; implemented `PrimeKnownFiles()`; clamped negative age in `ToSystemClock` |
| `src/runtime/vm/script_reloader.cc` | Reset all mutable state in `Start()`; call `PrimeKnownFiles()` before watcher start; update `file_reload_times_` in `ReloadAll` loop |

## Design Decisions

- **PrimeKnownFiles as a separate method**: Keeping the silent scan separate from `Start()` preserves the single-responsibility principle. `Start()` is "begin monitoring"; `PrimeKnownFiles()` is "initialize the file baseline." Callers that intentionally want the first scan to fire (e.g., for testing) can skip `PrimeKnownFiles()`.
- **State reset via Stop() + clear**: Rather than adding a dedicated `Reset()` method, `Start()` calls the existing `Stop()` (which safely joins any running watcher thread), then clears all collections. This reuses the established shutdown path.
