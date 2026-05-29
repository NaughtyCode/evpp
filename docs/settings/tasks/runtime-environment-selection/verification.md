# Verification Checklist: Runtime Environment Selection

## Unit Tests

- [ ] `--env=production` → MongoDB public config used
- [ ] `--env=development` → MongoDB dev config used
- [ ] `EVPP_ENV=staging` → staging profile loaded
- [ ] `--env=` CLI flag overrides `EVPP_ENV`
- [ ] Profile layering: common.json base + env.json overlay
- [ ] Invalid `--env=invalid` → startup fails with clear error
- [ ] No `--env` flag + no `EVPP_ENV` → defaults to development
- [ ] Release build can use dev MongoDB (previously impossible)

## Integration Tests

- [ ] Start server with `--env=production` → connects to public MongoDB
- [ ] Start server with `--env=development` → connects to dev MongoDB
- [ ] Profile JSON files loaded in correct order

## Manual Verification

- [ ] `./server --env=production` → log shows "environment: production"
- [ ] `EVPP_ENV=staging ./server` → log shows "environment: staging"
