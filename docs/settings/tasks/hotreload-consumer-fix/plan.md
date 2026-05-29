# Implementation Plan: Hot-Reload Consumer Fix

## Step 1: Redesign ReloadCallback with change details

Modify `config.h`:
- Define `ConfigChangeSet` struct: `std::vector<ConfigChangeEntry>` where each entry has `field_path`, `old_value`, `new_value`
- Change `ReloadCallback` from `std::function<void()>` to `std::function<void(const ConfigChangeSet&)>`
- Add `ConfigChangeSet Diff(const RuntimeConfig& old, const RuntimeConfig& new)` helper

## Step 2: Generate and pass change set in Reload()

Modify `config.cc` `Reload()`:
- Before swap, compute `ConfigChangeSet` by diffing old configs vs new configs
- Pass change set to `NotifyReloadCallbacks(changes)`

## Step 3: Wire reload to engine subsystems

Modify `engine.cc`:
- Replace log-only callback with actual consumer updates:
  - Frame: if `target_fps` changed, recalculate `frame_interval_` and update timer
  - Log: if `log.level` changed, reconfigure quill logger
  - Sandbox: if `sandbox_level` changed, log warning (cannot hot-change VM sandbox)

## Step 4: Add config FileWatcher

Modify `config.cc` (or new file):
- Reuse `FileWatcher` infrastructure from `ScriptReloader`
- Watch `resources/config/` directory for `.json` file changes
- Debounce (300ms) before triggering `Reload()`
- Add `ConfigManager::EnableAutoReload(bool)` toggle

## Step 5: Implement config rollback

Modify `config.h/cc`:
- Add `RuntimeConfig previous_runtime_config_` member
- In `Reload()`, save old config to `previous_*_` before swap
- Add `bool Rollback()` method: swaps previous config back
- Add `bool CanRollback() const` method

## Step 6: Add SIGHUP handler

Modify `engine.cc`:
- Add `sighup_watcher_` alongside existing `sigint_watcher_` / `sigterm_watcher_`
- On SIGHUP: call `ConfigManager::Instance().Reload(config_dir)`
- Log reload result (success or failure with errors)

## Step 7: Update tests

In `test_config.cpp`:
- Test: ReloadCallback receives correct change set
- Test: Reload with changed `target_fps` → frame_interval updated
- Test: Rollback after Reload restores previous config
- Test: CanRollback() returns false before first Reload
