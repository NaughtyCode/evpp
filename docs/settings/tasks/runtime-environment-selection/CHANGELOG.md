# Changelog: Runtime Environment Selection

## [Unreleased]

### Added
- (pending) `Environment` enum: `development`, `staging`, `production`
- (pending) `environment` field in `RuntimeConfig`
- (pending) `--env=` CLI flag
- (pending) `EVPP_ENV` environment variable support
- (pending) Config profile layering: `common.json` → `{env}.json` → CLI overrides
- (pending) Profile JSON templates in `resources/config/profiles/`

### Changed
- (pending) MongoDB selection from compile-time `#ifndef NDEBUG` to runtime `environment` field
- (pending) `ServerConfig`: added `active_mongodb` field

### Fixed
- (pending) P0-4: Same binary can now deploy to any environment
- (pending) P1-14: Profile hierarchy for dev/staging/prod
- (pending) P3-10: Config include/override mechanism
