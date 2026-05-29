# Verification Checklist: Observability & Tooling

## Unit Tests

- [ ] `Reload()` log contains field-level diff (old→new values)
- [ ] `Reload()` log contains summary (N fields changed)
- [ ] `Dump()` produces valid JSON
- [ ] `Dump()` output can be re-parsed by `LoadRuntimeFromString()`
- [ ] `DumpRuntime()` contains only RuntimeConfig fields
- [ ] `DumpServer()` contains only ServerConfig fields
- [ ] `ValidateOnly()` returns errors for invalid config
- [ ] `ValidateOnly()` does not modify running config
- [ ] `evpp_config_reload_total` incremented after Reload
- [ ] `evpp_config_reload_errors_total` incremented after Reload failure
- [ ] `evpp_config_reload_duration_seconds` recorded for Reload
- [ ] CLI validator: valid config → exit 0
- [ ] CLI validator: invalid JSON → exit 2
- [ ] CLI validator: validation error → exit 1
- [ ] CLI validator: `--verbose` shows per-file status
- [ ] Webhook POST sent after successful Reload
- [ ] Webhook failure logged but does not fail Reload

## Integration Tests

- [ ] `/config/snapshot` endpoint returns JSON matching Dump()
- [ ] Prometheus `/metrics` includes config_reload_total after Reload
- [ ] CI script: `evpp-config-validate --config-dir=resources/config` passes

## Manual Verification

- [ ] `curl http://localhost:8081/config/snapshot` → full config JSON
- [ ] `curl http://localhost:8081/metrics | grep config_` → config metrics present
- [ ] `./evpp-config-validate --config-dir=resources/config --verbose` → per-file OK/WARN/FAIL
