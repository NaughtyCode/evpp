# Changelog: Runtime Environment Selection

## [2026-05-29]

### Added
- `Environment` enum: `development`, `staging`, `production` (`config.h:20`)
- `ParseEnvironment()` / `EnvironmentToString()` / `EnvironmentFromEnvVar()` helpers (`config.h:22-40`)
- `environment` field in `RuntimeConfig` (default `"development"`) (`config.h:92`)
- `active_mongodb` field in `ServerConfig` — explicit override for MongoDB selection (`config.h:225`)
- `SetActiveEnvironment()` / `GetActiveEnvironment()` on ConfigManager (`config.h:291-292`)
- `ApplyProfileOverlay()` — merges `profiles/{env}.json` on top of base config (`config.cc`)
- `--env=` CLI flag in `server.cc` (overrides `EVPP_ENV`)
- `EVPP_ENV` environment variable support
- Profile JSON templates: `profiles/development.json`, `profiles/staging.json`, `profiles/production.json`

### Changed
- MongoDB selection: `engine.cc:178-182` — replaced compile-time `#ifndef NDEBUG` with runtime `environment`-based switch
- `config.cc` `Load()`: applies profile overlay after loading base runtime.json
- `config.cc` `Reload()`: applies profile overlay during reload
- `config.cc` `Diff()`: includes `environment` and `active_mongodb` fields
- `engine.cc` `Init()`: log message now includes `environment` field

### Fixed
- P0-4: Same binary can now deploy to any environment via `--env=` or `EVPP_ENV`
- P1-14: Profile hierarchy for dev/staging/prod via `profiles/{env}.json` overlay
- P3-10: Config override mechanism via profile layering (common.json → {env}.json → CLI)
