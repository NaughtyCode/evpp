# Config

JSON configuration files for the engine, powered by [glaze](https://github.com/stephenberry/glaze).

## Directory Layout

```
resources/config/
    runtime/
        runtime.json     System-level config: log, frame, runtime scripts_dir
    client/
        client.json      Client config: entry scripts_dir
    server/
        server.json      Server config: entry scripts_dir, HTTP, MessagePack
```

## Config Files

### runtime/runtime.json

| Field | Type | Default | Description |
|---|---|---|---|
| `resource_dir` | string | `"resources"` | Root directory for all resource files (scripts, physics, etc.) |
| `log.dir` | string | `"logs"` | Log output directory |
| `log.level` | string | `"info"` | Min log severity: trace, debug, info, warn, error, fatal |
| `log.rotation_size_mb` | int | 100 | Max log file size before rotation (MB) |
| `log.max_backup_files` | int | 10 | Number of rotated log files to retain |
| `log.format_pattern` | string | quill pattern | Log message format. Default includes `[thread_name:thread_id]` |
| `log.log_filename` | string | `""` | Optional log file prefix. Empty uses the current program name unless overridden by CLI |
| `frame.target_fps` | int | 30 | Target frames per second (0 = use interval_ms) |
| `frame.interval_ms` | int | 33 | Fallback frame interval in milliseconds |
| `frame.slow_threshold_multiplier` | int | 2 | Multiplier of interval_ms for "slow frame" detection |
| `scripts_dir` | string | `"resources/script/runtime"` | Shared runtime Lua scripts directory |

### client/client.json

| Field | Type | Default | Description |
|---|---|---|---|
| `scripts_dir` | string | `"resources/script/client"` | Client entry scripts directory |

### server/server.json

| Field | Type | Default | Description |
|---|---|---|---|
| `http.timeout_sec` | float | 10.0 | HTTP request timeout in seconds |
| `msgpack.max_nesting_depth` | int | 16 | Maximum MessagePack nesting depth |
| `scripts_dir` | string | `"resources/script/server"` | Server entry scripts directory |

## Usage

Config is loaded once at engine startup via `ConfigManager::Instance().Load("resources/config")`,
which reads all three subdirectories (`runtime/`, `client/`, `server/`). Client and server
configs are optional — only the runtime config is required.

Server CLI arguments (`--log_dir=`, `--log_prefix=`, `--scripts_dir=`) override the corresponding JSON values.
Client executable arguments support `--log_prefix=` as well.
