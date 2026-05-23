# Config

JSON configuration files for the engine, powered by [glaze](https://github.com/stephenberry/glaze).

## Files

- **engine.json** — System-level (engine) configuration: log, frame, scripts
- **server.json** — Server-level configuration: HTTP, MessagePack, application settings

## engine.json

| Field | Type | Default | Description |
|---|---|---|---|
| `log.dir` | string | `"logs"` | Log output directory |
| `log.level` | string | `"info"` | Min log severity: trace, debug, info, warn, error, fatal |
| `log.rotation_size_mb` | int | 100 | Max log file size before rotation (MB) |
| `log.max_backup_files` | int | 10 | Number of rotated log files to retain |
| `log.format_pattern` | string | quill pattern | Log message format (quill pattern syntax) |
| `frame.interval_ms` | int | 33 | Target frame interval in milliseconds (~30 FPS) |
| `frame.slow_threshold_multiplier` | int | 2 | Multiplier of interval_ms for "slow frame" detection |
| `scripts_dir` | string | `"resources/script"` | Lua scripts directory |

## server.json

| Field | Type | Default | Description |
|---|---|---|---|
| `http.timeout_sec` | float | 10.0 | HTTP request timeout in seconds |
| `msgpack.max_nesting_depth` | int | 16 | Maximum MessagePack nesting depth |

## Usage

Config is loaded once at engine startup via `ConfigManager::Instance().Load("resources/config")`.
Command-line arguments (`--log_dir=`, `--scripts_dir=`) override the corresponding JSON values.
