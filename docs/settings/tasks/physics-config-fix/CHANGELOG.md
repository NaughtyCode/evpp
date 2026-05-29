# Changelog: Physics Config Fix

## [Unreleased]

### Changed
- (pending) `error_on_unknown_keys` from `false` to `true` in all PhysicsConfigManager loaders
- (pending) Physics config directory renamed: `configs/` → `config/`
- (pending) Physics config files now require `"version": 1` field

### Added
- (pending) Schema documentation in `resources/physics/config/README.md`
- (pending) Version field to all 4 physics JSON config files

### Fixed
- (pending) P0-7: Unknown JSON keys in physics configs now cause load failure (not silent ignore)
- (pending) P3-6: Config directory naming now consistent (`config/` not `configs/`)
- (pending) P3-11: Unknown key behavior now consistent between ConfigManager and PhysicsConfigManager
