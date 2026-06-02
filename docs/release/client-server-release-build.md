# Client/Server Release Build

Use the Python release builder from the repository root:

```powershell
python scripts\build_release.py
```

On Windows, the batch wrapper is also available:

```bat
scripts\build_release.bat
```

The packaged launcher scripts pass extra arguments through to the executable,
so a custom log file prefix can be supplied externally:

```bat
run_server.bat --log_prefix=ShardA
run_client.bat --log_prefix=ClientA
```

When `--log_prefix` is omitted, the default prefix is the current program name,
for example `GameServer` or `GameClientApp`.

The script configures and builds these release targets:

- `GameServer`
- `GameClient`
- `GameClientApp`

It packages them under `artifacts/release/<Config>/` with a copy of
`resources/` and launcher scripts:

- `run_server.bat` / `run_server.sh`
- `run_client.bat` / `run_client.sh`

Start order is server first, then client. Project-specific client/server Lua
scripts should live in `resources/script/...` or be selected through config or
runtime overrides.

The C++ `GameClientApp` executable is only a host loop for `GameClient`; it
contains no game communication behavior.

## Verification

Useful checks from the repository root:

```powershell
python scripts\build_release.py --skip-configure --skip-build
python -m py_compile scripts\build_release.py
git diff --check
```
