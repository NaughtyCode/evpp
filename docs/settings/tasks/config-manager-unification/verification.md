# Verification Checklist: Config Manager Unification

## Unit Tests

- [ ] `ConfigManager` publicly inherits `IConfigManager`
- [ ] `PhysicsConfigManager` publicly inherits `IConfigManager`
- [ ] `ConfigManager::Validate()` returns validation result
- [ ] `PhysicsConfigManager::Validate()` returns validation result
- [ ] `ConfigManager::Dump()` produces valid JSON
- [ ] `PhysicsConfigManager::Dump()` produces valid JSON
- [ ] Factory `Create()` produces independent (non-singleton) instances
- [ ] Two factory instances have independent state
- [ ] MongoDB C++ members use snake_case
- [ ] MongoDB JSON keys still use camelCase (via `glaze::meta` mapping)
- [ ] `physics_scene_path` removed from `RuntimeConfig`
- [ ] `scene_path` added to `PhysicsConfig`
- [ ] Physics init reads `scene_path` from PhysicsConfig

## Integration Tests

- [ ] Server starts with refactored ConfigManager
- [ ] Physics system starts with refactored PhysicsConfigManager
- [ ] MongoDB config loaded with snake_case C++ members

## Manual Verification

- [ ] `grep -rn "readPreference\|maxPoolSize\|connectTimeoutMS" src/runtime/config/config.h` returns no camelCase members
- [ ] `grep -rn "physics_scene_path" src/runtime/config/config.h` returns no results
