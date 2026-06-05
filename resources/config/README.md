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
        redis.json       Optional Redis runtime config
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
| `hot_reload.enabled` | bool | true | Enables script hot-reload after runtime startup succeeds |
| `hot_reload.startup_delay_ms` | int | 60000 | Delay after successful runtime startup before script file watching starts |
| `hot_reload.poll_interval_ms` | int | 1000 | Script file watcher scan interval after hot-reload leaves idle state |
| `hot_reload.debounce_ms` | int | 300 | Per-file debounce window before hot-reload validates and reloads a changed script |
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
| `redis` | string | `""` | Redis config file path. Empty disables Redis workers |
| `redis_required` | bool | `false` | Fail startup/readiness when Redis config or Redis health is unavailable |
| `db_service` | string | `"resources/config/server/db_service.json"` | Database service config path |

### server/redis.json

| Field | Type | Default | Description |
|---|---|---|---|
| `connection.host` | string | `"127.0.0.1"` | Redis server host |
| `connection.port` | int | 6379 | Redis server port |
| `connection.username` | string | `""` | Optional ACL username |
| `connection.password` | string | `""` | Optional password; supports environment interpolation and is redacted in diffs/logs |
| `connection.database` | int | 0 | Database selected by Redis worker handshake |
| `connection.connect_timeout_ms` | int | 5000 | Worker startup and initial connection timeout |
| `connection.command_timeout_ms` | int | 5000 | Default command timeout for `redis.command` / `redis.eval` |
| `connection.keepalive` | bool | true | Enables TCP keepalive when supported |
| `queue.request_queue_size` | size_t | 4096 | Accepted but unsent request capacity |
| `queue.max_inflight` | size_t | 4096 | Max commands waiting for Redis replies |
| `queue.dispatch_batch_size` | size_t | 256 | Default Lua callback dispatch batch size |
| `thread.thread_count` | size_t | 4 | Number of `RedisClientThread` workers |
| `thread.main_loop_fps` | int | 60 | Redis worker VM/update loop rate |
| `script.redis_scripts_dir` | string | `"resources/script/redis"` | Redis worker private script directory |
| `script.auto_load` | bool | true | Loads Redis private scripts during worker startup |
| `log.enabled` | bool | true | Enables Redis module logging |
| `log.slow_command_ms` | int | 100 | Slow Redis command log threshold |
| `reconnect.enabled` | bool | true | Enables reconnect after disconnect |
| `reconnect.initial_delay_ms` | int | 500 | Initial reconnect delay |
| `reconnect.max_delay_ms` | int | 5000 | Max reconnect delay |
| `reconnect.backoff_multiplier` | int | 2 | Reconnect backoff multiplier |
| `reconnect.queue_while_disconnected` | bool | false | Allows accepted requests to queue while a worker is disconnected |

## Usage

Config is loaded once at engine startup via `ConfigManager::Instance().Load("resources/config")`,
which reads all three subdirectories (`runtime/`, `client/`, `server/`). Client and server
configs are optional — only the runtime config is required.

Redis is opt-in. Set `server.redis` to `resources/config/server/redis.json` to
load the Redis config and start Redis workers in server builds compiled with
`ENGINE_REDIS_ENABLED`. Leave it empty to disable Redis.

Server CLI arguments (`--log_dir=`, `--log_prefix=`, `--scripts_dir=`) override the corresponding JSON values.
Client executable arguments support `--log_prefix=` as well.
