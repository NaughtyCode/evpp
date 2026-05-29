# Changelog: Thread Safety Fix

## 2026-05-29 — Initial implementation

### Added
- `SetRuntimeOverride()` / `SetServerOverride()` — thread-safe config override setters
- `LoadMongoDbDevConfigLocked()` — TOCTOU-safe compound access
- `LoadMongoDbPublicConfigLocked()` — TOCTOU-safe compound access
- Temp object + lock pattern in all `Load*FromString` methods

### Changed
- `callbacks_mutex_` from `std::shared_mutex` to `std::mutex` (low-frequency op)
- All `Load*FromString` methods now parse to temp → validate → lock → swap
- `NotifyReloadCallbacks` uses `std::lock_guard<std::mutex>` instead of `shared_lock`

### Removed
- `GetRuntimeConfigMutable()` — replaced by `SetRuntimeOverride()`
- `GetClientConfigMutable()` — removed (no valid use case)
- `GetServerConfigMutable()` — replaced by `SetServerOverride()`

### Fixed
- P0-2: Data race — `Load*FromString` now holds lock during write
- P1-3: Mutable accessors no longer exposed in public API
- P3-1: Callback mutex simplified from shared_mutex to mutex

### Files changed
- `src/runtime/config/config.h` — removed mutable accessors, added safe setters + TOCTOU compound API, callback mutex type change
- `src/runtime/config/config.cc` — locked Load*FromString, callback mutex updates, TOCTOU-safe methods
- `src/server/server.cc` — updated CLI override path to use Set*Override
