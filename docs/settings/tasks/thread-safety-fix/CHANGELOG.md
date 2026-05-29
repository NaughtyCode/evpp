# Changelog: Thread Safety Fix

## [Unreleased]

### Added
- (pending) `std::lock_guard` in all `Load*FromString` methods
- (pending) `LoadMongoDbDevConfigLocked()` TOCTOU-safe compound API
- (pending) `LoadMongoDbPublicConfigLocked()` TOCTOU-safe compound API

### Changed
- (pending) `callbacks_mutex_` from `std::shared_mutex` to `std::mutex`

### Removed
- (pending) `GetRuntimeConfigMutable()` public API
- (pending) `GetClientConfigMutable()` public API
- (pending) `GetServerConfigMutable()` public API

### Fixed
- (pending) Data race: `LoadRuntimeFromString` writes to `runtime_config_` without lock
- (pending) Data race: `LoadClientFromString` writes to `client_config_` without lock
- (pending) Data race: `LoadServerFromString` writes to `server_config_` without lock
