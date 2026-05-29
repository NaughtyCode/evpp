# Implementation Plan: Runtime Environment Selection

## Step 1: Add environment field to RuntimeConfig

Modify `config.h`:
- Add `Environment` enum: `development`, `staging`, `production`
- Add `environment` field to `RuntimeConfig` (default: `development`)
- Add `active_mongodb` field to `ServerConfig` (replaces compile-time dev/public selection)

## Step 2: Add CLI and env var support

Modify `server.cc`:
- Add `--env=` CLI flag (overrides config file)
- Add `EVPP_ENV` environment variable check (lower priority than --env)
- Map string to enum: "development"/"dev", "staging"/"stage", "production"/"prod"

## Step 3: Replace compile-time MongoDB selection

Modify `engine.cc:178-182`:
- Replace `#ifndef NDEBUG` with `switch (runtime_cfg.environment)`:
  - `development` → use `mongo_dev_config_`
  - `staging` → use `mongo_dev_config_` (or dedicated staging)
  - `production` → use `mongo_public_config_`

## Step 4: Implement config profile layering

Modify `config.cc` `Load()`:
- Load order: `common.json` → `{environment}.json` → CLI/env overrides
- Each layer merges (overrides matching keys) rather than replaces
- Add `--config-profile=` CLI flag for custom profile file

## Step 5: Add profile JSON templates

Create new files in `resources/config/`:
- `profiles/development.json`
- `profiles/staging.json`
- `profiles/production.json`
- Each profile overrides relevant fields (log level, sandbox, ports, etc.)

## Step 6: Update tests

In `test_config.cpp`:
- Test: `--env=production` selects public MongoDB
- Test: `--env=development` selects dev MongoDB
- Test: Profile layering: common → env → CLI (verify override precedence)
- Test: Invalid `--env=invalid` exits with error
