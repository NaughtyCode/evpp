# Client/Server Release Build

For a step-by-step CS demo usage guide, see `docs/release/cs-demo-usage.md`.

Use the Python release builder from the repository root:

```powershell
python scripts\build_release.py --smoke
```

On Windows, the batch wrapper is also available:

```bat
scripts\build_release.bat --smoke
```

The script configures and builds these release targets:

- `GameServer`
- `GameClient`
- `GameClientApp`

It packages them under `artifacts/release/<Config>/` with a copy of `resources/` and launcher scripts:

- `run_server.bat` / `run_server.sh`
- `run_client.bat` / `run_client.sh`

Start order is server first, then client. The `--smoke` mode does that automatically and verifies that the Lua client completes the CS handshake and gameplay round trip.

Lua owns the client/server communication flow:

- `resources/script/shared/cs_protocol.lua` implements newline-framed JSON messages.
- `resources/script/server/init.lua` owns server-side session authority and handles `hello`, `ping`, `input`, and `chat`.
- `resources/script/client/init.lua` owns client connect/reconnect, handshake, ping, input upload, and server response handling.

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
- Only the standalone server starts `AdminHttpServer`; the client host runs in library mode and does not bind the server admin port.
- `git diff --check` reported only line-ending normalization warnings.
