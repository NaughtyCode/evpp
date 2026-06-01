# Client/Server Release Build

For a step-by-step independent CS demo usage guide, see `docs/release/cs-demo-usage.md`.

Use the Python release builder from the repository root:

```powershell
python scripts\build_release.py --smoke
```

On Windows, the batch wrapper is also available:

```bat
scripts\build_release.bat --smoke
```

For the CS demo specifically, use the dedicated batch script. It enables smoke
verification by default and passes any extra arguments through to
`scripts/build_release.py`:

```bat
scripts\build_cs_demo_release.bat
scripts\build_cs_demo_release.bat --skip-configure --smoke-timeout 25
```

The packaged launcher scripts pass extra arguments through to the executable, so a custom log file prefix can be supplied externally:

```bat
run_server.bat --log_prefix=ShardA
run_client.bat --log_prefix=ClientA
```

When `--log_prefix` is omitted, the default prefix is the current program name, for example `GameServer` or `GameClientApp`.

The script configures and builds these release targets:

- `GameServer`
- `GameClient`
- `GameClientApp`

It packages them under `artifacts/release/<Config>/` with a copy of `resources/`, the independent CS demo overlay, and launcher scripts:

- `run_server.bat` / `run_server.sh`
- `run_client.bat` / `run_client.sh`

Start order is server first, then client. The `--smoke` mode does that automatically and verifies that the Lua client completes the CS handshake and gameplay round trip.

The CS demo source lives outside the base runtime resources:

- `resources/demos/cs/script/shared/cs_protocol.lua`
- `resources/demos/cs/script/server/init.lua`
- `resources/demos/cs/script/client/init.lua`
- `resources/demos/cs/config/server/server.json`
- `resources/demos/cs/config/client/client.json`

During packaging the demo is copied into the release folder as an overlay:

- `resources/demos/cs/script/shared/cs_protocol.lua`
- `resources/demos/cs/script/server/init.lua`
- `resources/demos/cs/script/client/init.lua`

The demo config overlay points `server.scripts_dir` and `client.scripts_dir` at `resources/demos/cs/script/...`.

Use `--no-cs-demo` to package only the base resources. `--smoke` requires the CS demo overlay.

Lua owns the demo client/server communication flow:

- `resources/demos/cs/script/shared/cs_protocol.lua` implements newline-framed JSON messages.
- `resources/demos/cs/script/server/init.lua` owns server-side session authority and handles `hello`, `ping`, `input`, and `chat`.
- `resources/demos/cs/script/client/init.lua` owns client connect/reconnect, handshake, ping, input upload, and server response handling.

The C++ `GameClientApp` executable is only a host loop for `GameClient`; it contains no game communication behavior.

## Verification

Validated on Windows from the repository root:

```powershell
python scripts\build_release.py --skip-configure --smoke --smoke-timeout 25 --client-duration-ms 4000
python -m py_compile scripts\build_release.py
git diff --check
```

Result:

- Release artifacts generated under `artifacts/release/Release/`.
- Smoke passed with server started before client.
- Client log markers: `CS_CLIENT_CONNECTED 127.0.0.1:7777`, `CS_CLIENT_SENT hello`, `CS_CLIENT_WELCOME session=2`, `CS_CLIENT_SUCCESS session=2`.
- Server log markers: `CS_SERVER_LISTENING 127.0.0.1:7777`, `CS_SERVER_WELCOME player-2`.
- The CS demo server config disables the admin HTTP port so smoke only binds the gameplay port.
- `git diff --check` reported only line-ending normalization warnings.
