# Client/Server Release Build

Release packages are split by platform:

- Client: `scripts/build_client_release.py`
- Server: `scripts/build_server_release.py`
- All platforms: `scripts/build_all_platforms_release.py`
- Android all ABIs: `scripts/build_android_all_platforms_release.py`

The legacy `scripts/build_release.py` entry point is kept as an alias for the
all-platform release script.

## Version

Client and server use the same release version:

- Version file: `scripts/VERSION`

The version file uses SemVer, for example `0.1.0`, and must be tracked by Git.

Every release updates `scripts/VERSION` after packaging succeeds. The default
update is a patch bump. Use `--bump-version major|minor|patch` to choose the
part to bump, or `--version <semver>` to set a specific new version.

Commit the version file with the release changes:

```powershell
git add scripts\VERSION
git commit -m "Bump release version"
```

```powershell
python scripts\build_all_platforms_release.py --bump-version patch
```

## Commands

Build and package the client:

```powershell
python scripts\build_client_release.py
```

Build and package the server:

```powershell
python scripts\build_server_release.py
```

Build and package both platforms in one run:

```powershell
python scripts\build_all_platforms_release.py
```

Build and package Android artifacts for all supported ABIs:

```powershell
python scripts\build_android_all_platforms_release.py
```

The Android script defaults to `arm64-v8a`, `armeabi-v7a`, `x86`, and `x86_64`.
It auto-detects `artifacts/android-sdk/ndk/*`, `ANDROID_NDK_HOME`,
`ANDROID_NDK_ROOT`, Android SDK `ndk/` directories, or an explicit
`--android-toolchain-file=<ndk>/build/cmake/android.toolchain.cmake`.

On Windows, batch wrappers are available:

```bat
scripts\build_client_release.bat
scripts\build_server_release.bat
scripts\build_all_platforms_release.bat
scripts\build_android_all_platforms_release.bat
scripts\build_release.bat
```

## Output

Default output goes under `artifacts/release/<Config>/`:

- `GameCloud-<version>/` when using the all-platform script
- `GameCloudClient-<version>/`
- `GameCloudServer-<version>/`
- `all_platforms_manifest.json` when using the all-platform script

Android output goes under `artifacts/release/android/<Config>/`:

- `GameCloudAndroid-<abi>-<version>/`
- `android_all_platforms_manifest.json`

All-platform packages contain the client executable, client runtime library, and
server executable in the same directory:

- `GameClientApp`
- `GameClient`
- `GameServer`
- `resources/`
- `run_client`
- `run_server`
- `manifest.json`

Client packages contain:

- `GameClientApp`
- `GameClient`
- `resources/`
- `run_client`
- `manifest.json`

Server packages contain:

- `GameServer`
- `resources/`
- `run_server`
- `manifest.json`

Use `--flat-dist` to package under `artifacts/release/<Config>/client` or
`artifacts/release/<Config>/server` without a versioned directory. For the
all-platform script, `--flat-dist` packages under `artifacts/release/<Config>/full`.

## Runtime

The packaged launcher scripts pass extra arguments through to the executable.

Server example:

```bat
run_server.bat --env=production --log_prefix=ShardA
```

Multiple server processes can run from the same release directory by giving each
process a distinct instance id and admin port:

```bat
GameServer.exe --instance_id=shard-a --admin_port=18081
GameServer.exe --instance_id=shard-b --admin_port=18082
```

When `--instance_id` is set, the default PID file `server.pid` is automatically
resolved to `server-<instance_id>.pid`, and the default log prefix is suffixed
with the instance id. Use `--pid_file=<path>` for an explicit PID file, or
`--disable_pid_file` to disable PID locking for that process.

Client example:

```bat
run_client.bat --log_prefix=ClientA
```

The C++ `GameClientApp` executable is only a host loop for `GameClient`; it
contains no game communication behavior.

## Verification

Useful checks from the repository root:

```powershell
python scripts\build_client_release.py --skip-configure --skip-build
python scripts\build_server_release.py --skip-configure --skip-build
python scripts\build_all_platforms_release.py --skip-configure --skip-build
python scripts\build_android_all_platforms_release.py --skip-configure --skip-build --abi arm64-v8a
python -m py_compile scripts\release_common.py scripts\build_client_release.py scripts\build_server_release.py scripts\build_all_platforms_release.py scripts\build_android_all_platforms_release.py scripts\build_release.py
git diff --check
```

The release commands above update `scripts/VERSION`; commit or restore that
version change after verification.
