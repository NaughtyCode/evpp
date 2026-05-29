# Implementation Plan: Config Manager Unification

## Step 1: Define IConfigManager interface

Create `src/runtime/config/i_config_manager.h`:
```cpp
class IConfigManager {
public:
    virtual ~IConfigManager() = default;
    virtual bool Load(const std::string& config_dir) = 0;
    virtual bool Reload(const std::string& config_dir) = 0;
    virtual std::string Dump() const = 0;  // JSON snapshot
    virtual ConfigValidator::Result Validate() const = 0;
};
```

## Step 2: Refactor ConfigManager to implement IConfigManager

Modify `config.h`:
- `class ConfigManager : public IConfigManager`
- Add `Validate()` override
- Add `Dump()` override (serialize all configs to JSON)
- Add static factory method: `static std::unique_ptr<IConfigManager> Create()` for non-singleton instances

## Step 3: Refactor PhysicsConfigManager to implement IConfigManager

Modify `physics_config.h`:
- `class PhysicsConfigManager : public IConfigManager`
- Add `Validate()` override (wrap existing `ValidateConfigs()`)
- Add `Dump()` override
- Add `Reload()` override (full reload, not just thresholds/log level)
- Change `ReloadThresholds()` and `ReloadLogLevel()` to private helper methods

## Step 4: Fix MongoDB config naming

Modify `config.h`:
- Add `glaze::meta` specializations for all `MongoDb*` structs
- Map camelCase JSON keys to snake_case C++ members
- Rename C++ members: `readPreference` → `read_preference`, `maxPoolSize` → `max_pool_size`, etc.
- Update all code references to new member names

## Step 5: Move physics_scene_path

Modify `config.h`:
- Remove `physics_scene_path` from `RuntimeConfig`
- Add `scene_path` to `PhysicsConfig`

Modify `engine.cc`:
- Update physics init to read `scene_path` from PhysicsConfig instead of RuntimeConfig

## Step 6: Update tests

In test files:
- Test: ConfigManager implements IConfigManager interface
- Test: PhysicsConfigManager implements IConfigManager interface
- Test: Factory creates independent ConfigManager instances
- Test: Dump() produces valid JSON matching current config
- Test: MongoDB snake_case members work with existing JSON files
