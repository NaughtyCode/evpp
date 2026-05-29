# Changelog: Physics Config Fix

## 2026-05-29 — Initial implementation

### Changed
- `error_on_unknown_keys` from `false` to `true` in all 6 PhysicsConfigManager loader methods
- Physics config directory renamed: `resources/physics/configs/` → `resources/physics/config/`
- Physics JSON files stripped of underscore-prefixed metadata (`_module`, `_comment`, `_hot_reload`, `_requires_restart`, `_restart_note`)

### Added
- `int version = 1` field to `PhysicsConfig`, `ThreadingConfig`, `PhysicsLogConfig`, `ThresholdsConfig` C++ structs
- `glaze::meta` entries for `version` in all 4 physics config structs
- `"version": 1` field to all 4 physics JSON config files

### Fixed
- P0-7: Unknown JSON keys in physics configs now cause load failure (typo detection)
- P3-6: Config directory naming now consistent (`config/` not `configs/`)
- P3-11: Unknown key behavior now matches ConfigManager (both reject unknown keys)

### Files changed
- `src/runtime/physics/physics_config.cc` — 6x `error_on_unknown_keys = false` → `true`
- `src/runtime/physics/physics_config.h` — added `version` field + `glaze::meta` to all 4 structs
- `resources/physics/config/physics.json` — stripped metadata, added version
- `resources/physics/config/threading.json` — stripped metadata, added version
- `resources/physics/config/logging.json` — stripped metadata, added version
- `resources/physics/config/thresholds.json` — stripped metadata, added version
- `src/runtime/engine/engine.cc` — updated path `/physics/configs` → `/physics/config`
- `resources/physics/README.md` — updated path reference
