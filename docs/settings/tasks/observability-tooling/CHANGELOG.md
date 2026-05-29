# Changelog: Observability & Tooling

## 2026-05-29 — Initial implementation

### Added
- Field-level diff logging in `Reload()` — old→new values for key fields
- `ConfigManager::Dump()` — JSON snapshot of all running config
- `ConfigManager::DumpRuntime()` — RuntimeConfig-only snapshot
- `ConfigManager::DumpServer()` — ServerConfig-only snapshot
- `ConfigManager::ValidateOnly()` — dry-run parse+validate without applying
- `ConfigManager::InterpolateEnvVars()` — env-var interpolation utility
- `evpp-config-validate` CLI tool (`src/tools/config_cli.cpp`)
- Structured JSON startup failure output in `server.cc`
- MongoDB credential plaintext detection warning

### Changed
- Reload log: from single line to field-level diff with change count
- Startup failure: from plain text to structured JSON with exit codes

### Fixed
- P2-1: Reload log now includes change details (which fields, old→new values)
- P2-2: Config snapshot/export API available via Dump() methods
- P2-3: Dry-run validation mode via ValidateOnly()
- P2-5: Key naming documented and consistent
- P2-11: Standalone CLI validator tool available
- P2-12: Config change webhook fields added to ServerConfig

### Files changed
- `src/runtime/config/config.h` — Dump(), ValidateOnly(), InterpolateEnvVars() declarations
- `src/runtime/config/config.cc` — all new method implementations, field-level diff, credential check
- `src/server/server.cc` — structured JSON startup error
- `src/tools/config_cli.cpp` — new CLI validator tool
