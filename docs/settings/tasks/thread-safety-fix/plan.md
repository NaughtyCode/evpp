# Implementation Plan: Thread Safety Fix

## Step 1: Add lock to Load*FromString methods

Modify `config.cc`:
- `LoadRuntimeFromString()`: wrap `glz::read_json` + assignment with `std::lock_guard<std::shared_mutex>`
- `LoadClientFromString()`: same pattern
- `LoadServerFromString()`: same pattern (including `LoadMongoDbConfigsFromServer()` call)

## Step 2: Remove mutable accessors

Modify `config.h`:
- Remove `GetRuntimeConfigMutable()`, `GetClientConfigMutable()`, `GetServerConfigMutable()` from public API
- Update `server.cc:67-70` CLI override path: use `SetRuntimeOverride()` or pass via Init() params instead of direct mutable access

## Step 3: Simplify callback mutex

Modify `config.h` line 323 and `config.cc`:
- Replace `mutable std::shared_mutex callbacks_mutex_` with `mutable std::mutex callbacks_mutex_`
- Update `RegisterReloadCallback()`, `UnregisterReloadCallback()`, `NotifyReloadCallbacks()` to use `std::lock_guard<std::mutex>`

## Step 4: Add TOCTOU-safe MongoDB compound API

Modify `config.h/cc`:
- Add `LoadMongoDbDevConfigLocked(MongoDbConfig& out)` — holds shared_lock through path read + file load
- Add `LoadMongoDbPublicConfigLocked(MongoDbConfig& out)` — same pattern
- Document that standalone `GetMongoDbDevPath()` + `LoadMongoDbConfigFromFile()` is TOCTOU-prone

## Step 5: Update tests

In `test_config.cpp`:
- Add TSan-compatible concurrent test: thread A calls `LoadRuntimeFromString()` while thread B calls `GetRuntimeConfig()` — must not race
- Add test: `GetRuntimeConfigMutable()` no longer accessible (compile error if used)
