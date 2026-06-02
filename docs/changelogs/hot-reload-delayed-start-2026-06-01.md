# Hot-Reload Delayed File Watching

**Date:** 2026-06-01
**Status:** Complete
**Scope:** `src/runtime/engine`, `src/runtime/config`, `src/runtime/vm`,
`resources/config`

## Summary

Script hot-reload file watching now starts in an idle state during runtime
startup. After `Engine::Init()` completes successfully, the engine waits
`hot_reload.startup_delay_ms` before starting the `ScriptReloader` file watcher.
The default delay is `60000` ms.

## Changes

- Added `RuntimeConfig.hot_reload` with:
  - `enabled`
  - `startup_delay_ms`
  - `poll_interval_ms`
  - `debounce_ms`
- Moved `ScriptReloader::Start()` out of `Engine::Init()` startup work.
- Added an engine frame-loop check that starts the watcher only after the
  configured post-startup delay expires.
- Kept `ScriptReloader::PrimeKnownFiles()` behavior so enabling the watcher
  does not mass-reload every existing Lua file.
- Fixed `FileWatcher::PrimeKnownFiles()` to store native
  `std::filesystem::file_time_type` values, matching normal scan comparison
  and avoiding false-positive changes from clock conversion drift.
- Exposed hot-reload fields through Lua `config.get()` and the client runtime
  config scalar API.

## Verification

- `cmake --build artifacts/build --config Debug --target test_hotreload test_config`
- `artifacts\bin\Debug\test_hotreload.exe "[hotreload][filewatcher]"`
  - Passed: 23 assertions in 10 test cases.
- `artifacts\bin\Debug\test_config.exe "[config]"`
  - Passed: 229 assertions in 68 test cases.
- Temporary release-artifact delay test with `startup_delay_ms=500`,
  `poll_interval_ms=100`, and `debounce_ms=50`.
  - Passed: watcher started after the configured delay.
  - Passed: no pre-existing Lua files were reported as changed after priming.
