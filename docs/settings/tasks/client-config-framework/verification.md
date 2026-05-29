# Verification Checklist: Client Config Framework

## Unit Tests

- [ ] `RenderConfig` parsed from JSON with all fields
- [ ] `WindowConfig` parsed from JSON with all fields
- [ ] `InputConfig` parsed from JSON with all fields
- [ ] `AudioConfig` parsed from JSON with all fields
- [ ] `NetworkClientConfig` parsed from JSON with all fields
- [ ] `AssetConfig` parsed from JSON with all fields
- [ ] `UIConfig` parsed from JSON with all fields
- [ ] `PlatformConfig` parsed from JSON with all fields
- [ ] Layer 1 (defaults) → Layer 2 (factory JSON) → Layer 3 (user JSON) merge
- [ ] User layer overrides factory layer values
- [ ] Factory layer fills in missing user layer values (no data loss)
- [ ] `SaveClientLayer3()` serializes only user-overridden fields
- [ ] `SaveClientLayer3()` → `LoadClientLayer3()` preserves all values
- [ ] Corrupted `settings.json` → falls back to factory settings + warning
- [ ] `GetUserDataPath()` returns correct platform path

## Integration Tests

- [ ] Client app starts → loads factory config → loads user config → applies
- [ ] Modify setting in UI → `SaveClientLayer3()` → restart → setting persists

## Manual Verification

- [ ] `client.json` contains all config categories with sensible defaults
- [ ] Platform paths resolve correctly on each target platform
