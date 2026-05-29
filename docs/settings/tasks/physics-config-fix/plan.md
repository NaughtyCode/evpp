# Implementation Plan: Physics Config Fix

## Step 1: Enable error_on_unknown_keys

Modify `physics_config.cc`:
- Change all `glz::opts{.error_on_unknown_keys = false}` to `true` in:
  - `LoadPhysics()` (line 77)
  - `LoadThreading()` (line 94)
  - `LoadLogging()` (line 111)
  - `LoadThresholds()` (line 128)
  - `ReloadThresholds()` (line 243)
  - `ReloadLogLevel()` (line 272)

## Step 2: Add version field to physics configs

Modify all 4 physics JSON files:
- Add `"version": 1` to each file
- Add `int version = 1` field to `PhysicsConfig`, `ThreadingConfig`, `PhysicsLogConfig`, `ThresholdsConfig`
- Add `glaze::meta` entry for version field in each struct

## Step 3: Add schema documentation

Create `resources/physics/config/README.md`:
- Document all JSON fields for each config file
- Document valid ranges and constraints
- Document hot-reload support per field

## Step 4: Rename configs/ directory to config/

- Rename `resources/physics/configs/` → `resources/physics/config/`
- Update `engine.cc:216`: `auto phys_cfg = runtime_cfg.resource_dir + "/physics/config"`
- Update any other hardcoded references

## Step 5: Add missing key warnings

Modify `physics_config.cc`:
- Add optional `error_on_missing_keys = true` with a migration period
- Or: after parse, diff JSON keys against struct fields, warn on missing

## Step 6: Update tests

In test files:
- Test: unknown key in physics.json → load fails
- Test: typo key (e.g., `gravityy` instead of `gravityY`) → load fails with clear error
- Test: version field present and valid
