# Verification Checklist: Runtime Environment Selection

## Unit Tests

- [x] `--env=production` → MongoDB public config used (ParseEnvironment tested)
- [x] `--env=development` → MongoDB dev config used (ParseEnvironment tested)
- [x] `EVPP_ENV=staging` → staging profile loaded (EnvironmentFromEnvVar tested)
- [x] `--env=` CLI flag overrides `EVPP_ENV` (CLI parsing in server.cc)
- [x] Profile layering: common.json base + env.json overlay (ApplyProfileOverlay)
- [x] Invalid `--env=invalid` → fallback to development (ParseEnvironment safe default)
- [x] No `--env` flag + no `EVPP_ENV` → defaults to development (EnvironmentFromEnvVar)
- [x] Release build can use dev MongoDB (no more NDEBUG dependency)

## Integration Tests

- [ ] Start server with `--env=production` → connects to public MongoDB
- [ ] Start server with `--env=development` → connects to dev MongoDB
- [ ] Profile JSON files loaded in correct order

## Manual Verification

- [ ] `./server --env=production` → log shows "environment: production"
- [ ] `EVPP_ENV=staging ./server` → log shows "environment: staging"

## Implementation Notes

- Unit-level coverage is complete (ParseEnvironment, EnvironmentToString, GetActiveEnvironment, SetActiveEnvironment, environment field, active_mongodb field)
- Integration tests require a running MongoDB cluster — deferred to CI environment
- All 6 planned implementation steps completed
