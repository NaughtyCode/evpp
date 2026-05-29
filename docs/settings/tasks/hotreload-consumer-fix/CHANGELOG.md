# Changelog: Hot-Reload Consumer Fix

## [Unreleased]

### Added
- (pending) `ConfigChangeSet` struct with field-level diff info
- (pending) `ConfigChangeEntry` struct (field_path, old_value, new_value)
- (pending) `ConfigManager::Rollback()` method
- (pending) `ConfigManager::CanRollback()` method
- (pending) `ConfigManager::EnableAutoReload(bool)` toggle
- (pending) Config FileWatcher (reuses ScriptReloader's FileWatcher infra)
- (pending) SIGHUP signal handler for config reload
- (pending) `Diff()` helper for computing config changes

### Changed
- (pending) `ReloadCallback` signature: `void()` → `void(const ConfigChangeSet&)`
- (pending) `Reload()` now computes and passes change set to callbacks
- (pending) Engine reload callback now actually applies changes (frame, log, sandbox)

### Fixed
- (pending) P0-3: Config hot-reload callbacks only logged, didn't propagate changes
- (pending) P1-12: No FileWatcher for config files — manual reload only
- (pending) P1-11: No rollback mechanism for bad config
