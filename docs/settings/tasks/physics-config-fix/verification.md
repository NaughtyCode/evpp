# Verification Checklist: Physics Config Fix

## Unit Tests

- [ ] Unknown key in physics.json → `Load()` returns false
- [ ] Unknown key in threading.json → `Load()` returns false
- [ ] Unknown key in logging.json → `Load()` returns false
- [ ] Unknown key in thresholds.json → `Load()` returns false
- [ ] Typo key `gravityy` (should be `gravityY`) → load fails with clear error message
- [ ] Valid physics.json with all known keys → `Load()` succeeds
- [ ] Version field present and matches expected value
- [ ] Missing field in physics.json → warning logged (or error, depending on config)

## Integration Tests

- [ ] Physics system initializes with valid config in `config/` (renamed) directory
- [ ] Physics system fails to initialize with unknown key in config

## Manual Verification

- [ ] `ls resources/physics/config/` → directory renamed from `configs/`
- [ ] Add typo to physics.json → restart → error message shows which key is unknown
