# Changelog: Hot-Reload Consumer Fix

## [2026-05-29] — Task complete

### Added
- `ConfigChangeSet` struct with field-level diff info (`ConfigChangeEntry`: field_path, old_value, new_value)
- `ConfigManager::Diff()` — computes change set between old and new configs
- `ConfigManager::Rollback()` — restores previous config snapshot
- `ConfigManager::CanRollback()` — checks if snapshot is available
- `ConfigManager::EnableAutoReload()` / `DisableAutoReload()` / `IsAutoReloadEnabled()` — FileWatcher-based auto-reload
- SIGHUP signal handler in `Engine::Start()` — triggers `ConfigManager::Reload()` on Unix
- `Engine::ApplyConfigChanges()` — main-thread application of hot-reloaded config changes

### Changed
- `ReloadCallback` signature: `void()` → `void(const ConfigChangeSet&)`
- `Reload()` now saves previous config for rollback, builds change set, passes to callbacks
- `NotifyReloadCallbacks()` now accepts and forwards `ConfigChangeSet`
- `LoadRuntimeFromString()` now saves previous config snapshot (enables rollback testing)
- Engine reload callback replaced with pending-changes pattern (main-thread dispatch via `FrameLoop`)
- Engine callback applies: frame_interval recalculation + timer reschedule, log level warning, sandbox_level warning

### Fixed
- P0-3: Config hot-reload callbacks now propagate changes to engine subsystems
- P1-12: Config FileWatcher added — `.json` files in config dir trigger auto-reload
- P1-11: Config rollback implemented — `Rollback()` restores pre-reload snapshot
