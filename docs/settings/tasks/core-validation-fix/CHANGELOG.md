# Changelog: Core Validation Fix

## 2026-05-29 — Initial implementation

### Added
- `ConfigValidator::ValidateServer()` — validates admin_port, http.timeout_sec, msgpack limits
- `ConfigValidator::ValidateClient()` — validates scripts_dir not empty
- `ConfigValidator::ValidateAll()` — bulk validation of all configs
- `ConfigValidator::ValidateCross()` — cross-field validation (admin_port vs scripts_dir)
- `CheckEnum()` helper — validates string values against allowed set
- `CheckWarning()` helper — non-fatal diagnostic messages
- `sandbox_level` enum validation (strict/server/full) — rejects typos
- `log.level` enum validation (trace/debug/info/warn/error/critical)
- `frame.slow_threshold_multiplier` range validation [1, 100]
- `log.max_backup_files` range validation [0, 1000]
- Cross-field warning: `target_fps` vs `interval_ms` consistency check
- Warning for `msgpack.max_payload_size > 100MB`

### Changed
- `LoadRuntimeFromString()` — calls `ConfigValidator::Validate()` after parse
- `LoadRuntimeFromFile()` — calls `ConfigValidator::Validate()` after parse
- `LoadServerFromString()` — calls `ConfigValidator::ValidateServer()` after parse
- `LoadServerFromFile()` — calls `ConfigValidator::ValidateServer()` after parse
- `Load()` — calls cross-field validation after all configs loaded
- `Reload()` — validates runtime and server temp configs before lock+swap

### Fixed
- P0-1: `ConfigValidator::Validate()` is now called in all Load/Reload paths
- P1-4: Cross-field validation added (target_fps vs interval_ms, admin_port vs scripts_dir)

### Files changed
- `src/runtime/config/config_validator.h` — new methods and helpers
- `src/runtime/config/config_validator.cc` — full reimplementation
- `src/runtime/config/config.cc` — wired validators into Load/Reload
- `src/tests/unit/config/test_config.cpp` — 18 new test cases
