# Changelog: Config Manager Unification

## [Unreleased]

### Added
- (pending) `IConfigManager` abstract interface (Load, Reload, Validate, Dump)
- (pending) `ConfigManager::Create()` factory for non-singleton instances
- (pending) `ConfigManager::Validate()` method
- (pending) `ConfigManager::Dump()` method (JSON snapshot)
- (pending) `PhysicsConfigManager::Validate()` method
- (pending) `PhysicsConfigManager::Dump()` method
- (pending) `PhysicsConfigManager::Reload()` full reload method
- (pending) `glaze::meta` mappings for all MongoDB structs (camelCase JSON → snake_case C++)

### Changed
- (pending) `ConfigManager` now inherits `IConfigManager`
- (pending) `PhysicsConfigManager` now inherits `IConfigManager`
- (pending) MongoDB C++ members renamed to snake_case
- (pending) `physics_scene_path` moved from `RuntimeConfig` to `PhysicsConfig`

### Fixed
- (pending) P1-1: Dual config managers now share common interface
- (pending) P1-5: MongoDB config naming now consistent (snake_case C++ members)
- (pending) P3-3: physics_scene_path now on PhysicsConfig (correct subsystem boundary)
- (pending) P3-11: Validation patterns unified across managers
