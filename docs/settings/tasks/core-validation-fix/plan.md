# Implementation Plan: Core Validation Fix

## Step 1: Wire ConfigValidator into Load path

Modify `ConfigManager::Load()` in `config.cc`:
- After `LoadRuntimeFromFile()` succeeds, call `ConfigValidator::Validate(runtime_config_)`
- If validation fails, return false (do not proceed with partial config)
- For `LoadServerFromFile()`, add new `ValidateServerConfig()` call
- For `LoadClientFromFile()`, add new `ValidateClientConfig()` call

## Step 2: Wire ConfigValidator into Reload path

Modify `ConfigManager::Reload()` in `config.cc` line ~108-177:
- After parsing all three configs to temp objects (before lock+swap), validate each
- If any validation fails, return false (old config preserved)
- Log specific validation errors

## Step 3: Expand ConfigValidator coverage

Modify `config_validator.h/cc`:
- Add `ValidateServerConfig(const ServerConfig&)` method:
  - `admin_port` in [0, 65535]
  - `http.timeout_sec` > 0 and <= 300
  - `msgpack.max_nesting_depth` in [1, 256]
  - `msgpack.max_payload_size` > 0
- Add `ValidateClientConfig(const ClientConfig&)` method:
  - `scripts_dir` not empty
- Add cross-field validation: `interval_ms` should equal `1000 / target_fps` (warning, not error)

## Step 4: Add sandbox_level enum validation

In `config_validator.cc`:
- Validate `sandbox_level` against known values: `"strict"`, `"server"`, `"full"`
- Reject unknown sandbox strings with clear error message

## Step 5: Add error_on_missing_keys warnings

In `config.cc` `Load*` methods:
- After successful parse, compare JSON keys with expected struct fields
- Log WARN for any JSON field that is missing (using C++ default)
- This is a non-fatal diagnostic: helps catch copy-paste config errors

## Step 6: Update tests

In `test_config.cpp`:
- Add test: Load valid config → validation passes
- Add test: Load config with `target_fps=0` → validation fails
- Add test: Load config with `admin_port=-1` → validation fails
- Add test: Load config with `http.timeout_sec=0` → validation fails
- Add test: Load config with unknown `sandbox_level` → validation fails
- Add test: Load config with `target_fps=60, interval_ms=33` → cross-field warning logged
