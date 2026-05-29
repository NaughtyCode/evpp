# Verification Checklist: Hot-Reload Consumer Fix

## Unit Tests

- [ ] ReloadCallback receives ConfigChangeSet with changed field name and old/new values
- [ ] ReloadCallback with no changes receives empty change set
- [ ] Frame: reload with new `target_fps` → `frame_interval_` recalculated
- [ ] Log: reload with new `log.level` → logger reconfigures
- [ ] Rollback() after Reload restores previous config values
- [ ] Rollback() without prior Reload is no-op
- [ ] CanRollback() returns correct state
- [ ] FileWatcher detects config file change → triggers Reload
- [ ] FileWatcher debounce: rapid changes trigger single Reload
- [ ] SIGHUP triggers Reload (Unix only)

## Integration Tests

- [ ] Start server → modify runtime.json → send SIGHUP → verify new config active
- [ ] Start server → modify runtime.json (FileWatcher enabled) → verify auto-reload
- [ ] Reload with bad config → old config preserved → CanRollback still true
- [ ] Rollback after bad config → old config active

## Manual Verification

- [ ] `kill -HUP <pid>` triggers "config reloaded" log with field-level diff
- [ ] Modify `target_fps` in JSON → SIGHUP → frame rate changes at runtime
