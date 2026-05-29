# Implementation Plan: Config Manager Unification

## Step 1: Define IConfigManager interface [DONE]

Create `src/runtime/config/i_config_manager.h`:
- `ValidationResult` struct with `valid`, `errors`, `warnings`
- `IConfigManager` abstract interface: `Load()`, `Reload()`, `Validate()`, `Dump()`

## Step 2: Refactor ConfigManager to implement IConfigManager [DONE]

Modify `config.h` / `config.cc`:
- `class ConfigManager : public IConfigManager`
- Add `Validate()` override — validates current in-memory config
- Add `static std::unique_ptr<IConfigManager> Create()` factory for non-singleton instances
- `Dump()` now marked `override`

## Step 3: Refactor PhysicsConfigManager to implement IConfigManager [DONE]

Modify `physics_config.h` / `physics_config.cc`:
- `class PhysicsConfigManager : public IConfigManager`
- Add `Validate()` override — wraps existing `ValidateConfigs()`, returns `ValidationResult`
- Add `Dump()` override — serializes all 4 configs to JSON
- Add `Reload()` override — full reload of all 4 files with validation + rollback
- Keep `ReloadThresholds()` and `ReloadLogLevel()` as public for selective hot-reload

## Step 4: Fix MongoDB config naming [DONE]

Modify `config.h`:
- Renamed all MongoDB C++ struct members to snake_case
- Added `glaze::meta` specializations for all `MongoDb*` structs
- JSON keys remain camelCase (matching existing config files)
- No external code changes needed — `connection.uri` was already snake_case

## Step 5: Move physics_scene_path [DONE]

Modify `config.h`:
- Removed `physics_scene_path` from `RuntimeConfig`

Modify `physics_config.h`:
- Added `scene_path` to `PhysicsConfig` with default `"/physics/data/scene.json"`
- Added `"scenePath"` → `scene_path` mapping in `glaze::meta`

Modify `engine.cc`:
- Physics init reads `scene_path` from PhysicsConfig (loaded inside Initialize)

Modify `physics_engine_bridge.h/.cc`:
- Removed `assets_path` parameter from `Initialize()`

Modify `physics_system.h/.cc`:
- Removed `assets_path` parameter from `Initialize()`
- Computes `assets_path_` from `PhysicsConfig.scene_path` after config loading

Modify `config_bind.cc`:
- Removed `physics_scene_path` from Lua `config.get()` (now on PhysicsConfig)

Modify `config.cc`:
- Removed `physics_scene_path` from `Diff()` change tracking

## Step 6: Update tests [DEFERRED]

Test files need updating for the new interface. Key test cases:
- ConfigManager implements IConfigManager interface
- PhysicsConfigManager implements IConfigManager interface
- Factory creates independent ConfigManager instances
- Dump() produces valid JSON matching current config
- MongoDB snake_case members work with existing JSON files
