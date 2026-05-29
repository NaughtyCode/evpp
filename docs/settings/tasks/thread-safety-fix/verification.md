# Verification Checklist: Thread Safety Fix

## Unit Tests

- [ ] `LoadRuntimeFromString()` + concurrent `GetRuntimeConfig()` — no data race (TSan clean)
- [ ] `LoadClientFromString()` + concurrent `GetClientConfig()` — no data race
- [ ] `LoadServerFromString()` + concurrent `GetServerConfig()` — no data race
- [ ] `RegisterReloadCallback()` + concurrent `NotifyReloadCallbacks()` — no deadlock
- [ ] `LoadMongoDbDevConfigLocked()` returns consistent path+data
- [ ] Mutable accessors removed: code using them fails to compile

## Thread Sanitizer

- [ ] Build with `-fsanitize=thread` — run full test suite — zero warnings
- [ ] Build with `-fsanitize=thread` — run smoke tests — zero warnings

## Manual Verification

- [ ] `grep -rn "GetRuntimeConfigMutable\|GetClientConfigMutable\|GetServerConfigMutable" src/` returns only deprecation comments
