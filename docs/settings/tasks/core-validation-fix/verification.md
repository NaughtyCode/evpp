# Verification Checklist: Core Validation Fix

## Unit Tests

- [ ] `ConfigValidator::Validate()` rejects `target_fps=0`
- [ ] `ConfigValidator::Validate()` rejects `target_fps>1000`
- [ ] `ConfigValidator::Validate()` rejects `interval_ms=0`
- [ ] `ConfigValidator::Validate()` rejects `interval_ms>10000`
- [ ] `ConfigValidator::Validate()` rejects empty `resource_dir`
- [ ] `ConfigValidator::Validate()` rejects empty `scripts_dir`
- [ ] `ConfigValidator::Validate()` rejects `rotation_size_mb=0`
- [ ] `ConfigValidator::Validate()` rejects `rotation_size_mb>10240`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `admin_port=-1`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `admin_port=65536`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `http.timeout_sec=0`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `http.timeout_sec>300`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `msgpack.max_nesting_depth=0`
- [ ] `ConfigValidator::ValidateServerConfig()` rejects `msgpack.max_payload_size=0`
- [ ] Cross-field: `target_fps=60, interval_ms=33` logs warning (1000/60≠33)
- [ ] Cross-field: `target_fps=30, interval_ms=33` passes (1000/30≈33)
- [ ] Sandbox: `"strict"` passes, `"stirct"` (typo) fails
- [ ] Sandbox: `"full"` passes, `"none"` fails
- [ ] `Load()` with invalid config returns false
- [ ] `Reload()` with invalid new config preserves old config

## Integration Tests

- [ ] Server starts with valid config directory
- [ ] Server exits with error on invalid config
- [ ] `Load()` flow: parse → validate → apply (all-or-nothing)

## Manual Verification

- [ ] Run `test_config.exe` — all tests pass
- [ ] Run `smoke_config_load.exe` — smoke test passes
