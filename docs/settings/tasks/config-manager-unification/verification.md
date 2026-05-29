# Verification Checklist: Config Manager Unification

## Unit Tests

- [x] `ConfigManager` publicly inherits `IConfigManager`
- [x] `PhysicsConfigManager` publicly inherits `IConfigManager`
- [x] `ConfigManager::Validate()` returns validation result
- [x] `PhysicsConfigManager::Validate()` returns validation result
- [x] `ConfigManager::Dump()` produces valid JSON
- [x] `PhysicsConfigManager::Dump()` produces valid JSON
- [x] Factory `Create()` produces independent (non-singleton) instances
- [x] Two factory instances have independent state
- [x] MongoDB C++ members use snake_case
- [x] MongoDB JSON keys still use camelCase (via `glaze::meta` mapping)
- [x] `physics_scene_path` removed from `RuntimeConfig`
- [x] `scene_path` added to `PhysicsConfig`
- [x] Physics init reads `scene_path` from PhysicsConfig

## Integration Tests

- [x] Server starts with refactored ConfigManager
- [x] Physics system starts with refactored PhysicsConfigManager
- [x] MongoDB config loaded with snake_case C++ members

## Manual Verification

- [x] `grep -rn "readPreference\|maxPoolSize\|connectTimeoutMS" src/runtime/config/config.h` returns only JSON key strings in glaze::meta
- [x] `grep -rn "physics_scene_path" src/runtime/config/config.h` returns no results
