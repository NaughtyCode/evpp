# Changelog: Game Business Config Framework

## [Unreleased]

### Added
- (pending) `SandboxLevel` enum (Strict, Server, Full) replacing free-form string
- (pending) Lua `config.get(path)` API — read any config value by dotted path
- (pending) Lua `config.get_module(name)` API — load named config module as Lua table
- (pending) Lua `config.on_change(module, callback)` API — register hot-reload callback
- (pending) `ConfigTable` class — typed data table loading (JSON + CSV)
- (pending) `ConfigTable` type inference (int, float, bool, string)
- (pending) `ConfigTable` index support for fast ID-based lookup
- (pending) `ReferenceValidator` — cross-file reference integrity checking
- (pending) Example data tables: monster.json, item.json, drop_table.json, skill.json
- (pending) Lua config bindings export

### Fixed
- (pending) P1-15: Lua layer now has config API
- (pending) P1-16: Reference integrity validation implemented
- (pending) P2-14: Lua `config.get()` API available
- (pending) P2-16: Dual-end config sharing infrastructure
- (pending) P2-17: Config table import pipeline (JSON + CSV)
- (pending) P2-18: Cross-system config reference management
