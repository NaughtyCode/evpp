# Implementation Plan: Observability & Tooling

## Step 1: Field-level diff logging in Reload

Modify `config.cc` `Reload()`:
- After computing old vs new configs, generate field-level diff
- Log each changed field: `"config.runtime.frame.target_fps: 30 → 60"`
- Log summary: `"config reloaded: 3 fields changed, 45 unchanged"`

## Step 2: Implement config snapshot/export API

Modify `config.h/cc`:
- `ConfigManager::Dump()` → serialize all current configs to single JSON
- `ConfigManager::DumpRuntime()` → serialize only RuntimeConfig
- `ConfigManager::DumpServer()` → serialize only ServerConfig
- Use glaze `write_json` for serialization

Add HTTP endpoint:
- `GET /config/snapshot` → returns full config JSON (protected by admin bind address)

## Step 3: Add Prometheus config metrics

Modify `metrics.h`:
- `evpp_config_reload_total` (counter): incremented on each Reload call
- `evpp_config_reload_errors_total` (counter): incremented on each Reload failure
- `evpp_config_reload_duration_seconds` (histogram): Reload() wall-clock time
- `evpp_config_hash` (gauge): hash of current config content (for drift detection)

## Step 4: Add dry-run validation mode

Modify `config.h/cc`:
- `ConfigManager::ValidateOnly(config_dir)` → parse + validate, return result, don't apply
- Returns `ConfigValidator::Result` with all errors/warnings
- Expose via CLI: `./server --validate-config --config-dir=path`

## Step 5: Build standalone CLI validator

Create `src/tools/config_cli.cpp`:
- Standalone binary: `evpp-config-validate`
- Usage: `evpp-config-validate [--config-dir=PATH] [--verbose]`
- Exit code 0 = all valid, 1 = validation errors, 2 = parse errors
- Verbose mode: prints each file and its validation status
- Add CMakeLists.txt entry for new target

## Step 6: Add config change webhook

Modify `config.cc`:
- After successful Reload, POST to configured webhook URL
- Body: `{"timestamp": "...", "changed_fields": [...], "config_hash": "..."}`
- Configurable in ServerConfig: `webhook_url`, `webhook_timeout_sec`
- Failure to POST webhook is logged but does not fail the Reload

## Step 7: Add structured startup error output

Modify `server.cc`:
- On config load/validation failure, print JSON to stderr:
  ```json
  {"event":"startup_failed","exit_code":3,"reason":"parse error","file":"runtime.json","line":42,"message":"unexpected token"}
  ```
- Enables CI/CD and monitoring systems to parse failure reason

## Step 8: Update tests

In test files:
- Test: Dump() produces valid JSON matching loaded config
- Test: Dump() → parse Dump() output → values match original
- Test: ValidateOnly returns errors without modifying config
- Test: Prometheus metrics emitted after Reload
- Test: CLI validator exits 0 for valid config, non-zero for invalid
