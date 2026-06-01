# Independent CS Demo Overlay - 2026-06-01

## Context

The Lua client/server demo was previously stored directly in the base
`resources/script/client`, `resources/script/server`, and `resources/script/shared`
directories. That mixed demo gameplay logic with the reusable runtime resource
tree.

## Changes

- Moved the CS demo source into `resources/demos/cs/`.
- Added demo-specific client and server config overlays under `resources/demos/cs/config/`.
- Updated `scripts/build_release.py` to package the demo as an overlay under
  `resources/demos/cs/script` in release artifacts.
- Kept base `resources/script/client` and `resources/script/server` as clean
  project entry-script directories with README files only.
- Added `--no-cs-demo` for packaging base resources without the demo overlay.
- Added `scripts/build_cs_demo_release.bat` as the dedicated Windows CS demo
  release script. It enables smoke verification by default.
- Disabled the demo server admin HTTP port through the demo server config so the
  smoke run only binds the gameplay port.
- Updated release and CS demo usage documentation to reference the independent
  source and packaged paths.

## Verification

Ran on Windows from the repository root:

```powershell
python -m py_compile scripts\build_release.py
cmd /c scripts\build_release.bat --skip-configure --smoke --smoke-timeout 25 --client-duration-ms 4000
cmd /c scripts\build_cs_demo_release.bat --skip-configure --smoke-timeout 25 --client-duration-ms 4000
python scripts\build_release.py --skip-configure --skip-build --no-cs-demo --dist-dir artifacts\release-no-demo
cd artifacts\release\Release
.\run_server.bat --log_prefix=GameServerLauncherSmoke
.\run_client.bat --log_prefix=GameClientLauncherSmoke --duration_ms=4000 --tick_ms=16
```

Result:

- Release build completed for `GameServer`, `GameClient`, and `GameClientApp`.
- CS smoke passed: `client connected to server and completed Lua CS round trip`.
- Packaged demo files were present under `artifacts/release/Release/resources/demos/cs/script/`.
- Packaged base `resources/script/client` and `resources/script/server` contained README files only.
- Server loaded `entry_scripts_dir=[resources/demos/cs/script/server]`.
- Client loaded `entry_scripts_dir=[resources/demos/cs/script/client]`.
- Client markers observed: `CS_CLIENT_CONNECTED`, `CS_CLIENT_SENT hello`,
  `CS_CLIENT_WELCOME session=2`, `CS_CLIENT_SUCCESS session=2`.
- Server markers observed: `CS_SERVER_LISTENING 127.0.0.1:7777`,
  `CS_SERVER_WELCOME player-2`.
- `--no-cs-demo` packaging completed without `resources/demos/cs`; base
  client/server script directories contained README files only.
- Packaged launcher scripts were tested end-to-end and observed the same
  `CS_CLIENT_SUCCESS` result while loading `resources/demos/cs/script/...`.
