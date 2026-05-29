# Changelog: Config Manager Unification

## [2026-05-29]

### Added
- `IConfigManager` abstract interface (Load, Reload, Validate, Dump)
- `ValidationResult` struct — unified validation return type
- `ConfigManager::Create()` factory for non-singleton instances
- `ConfigManager::Validate()` method
- `ConfigManager::Dump()` method (JSON snapshot)
- `PhysicsConfigManager::Validate()` method
- `PhysicsConfigManager::Dump()` method
- `PhysicsConfigManager::Reload()` full reload method
- `glaze::meta` mappings for all MongoDB structs (camelCase JSON → snake_case C++)
- `scene_path` field on `PhysicsConfig` with `scenePath` JSON key

### Changed
- `ConfigManager` now inherits `IConfigManager`
- `PhysicsConfigManager` now inherits `IConfigManager`
- MongoDB C++ members renamed to snake_case
- `physics_scene_path` moved from `RuntimeConfig` to `PhysicsConfig.scene_path`
- `PhysicsEngineBridge::Initialize` signature: removed `assets_path` parameter
- `PhysicsSystem::Initialize` signature: removed `assets_path` parameter
- `ConfigValidator::Result` now aliases `ValidationResult`

### Fixed
- P1-1: Dual config managers now share common interface
- P1-5: MongoDB config naming now consistent (snake_case C++ members)
- P3-3: physics_scene_path now on PhysicsConfig (correct subsystem boundary)
- P3-11: Validation patterns unified across managers
