# Config System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | `config.get()` / `config.get_module()` 为同步读取。`config.flush_changes()` 必须在 owning `lua_State` 所属线程调用，因为它会执行 Lua 回调。 |
| **线程安全** | `ConfigManager` 读写由内部锁保护；Lua binding 保存 registry callback ref，Lua API 本身不可跨 VM/跨线程直接调用。 |
| **回调线程** | `config.on_change()` 的底层 reload 回调可能从任意线程入队变更；实际 Lua callback 只在脚本线程调用 `config.flush_changes()` 时执行。 |

## Overview

The Config system manages engine configuration loaded from JSON files at startup and exposes selected runtime/server values plus data-module loading and hot-reload notifications to Lua.

## Module

`config` (global table)

## Lua Functions

### `config.get(path)`

Returns a selected scalar value from the active runtime/server config, or `nil` when the path is not exposed.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `path` | `string` | `const char*` (via `luaL_checkstring`) | Dot-separated config path |

| Returns | Type | Description |
|---------|------|-------------|
| `value` | `string` / `integer` / `number` / `boolean` / `nil` | Value copied from `ConfigManager::Instance()` |

Exposed paths:

| Prefix | Paths |
|--------|-------|
| Runtime top-level | `resource_dir`, `scripts_dir`, `sandbox_level`, `environment` |
| Runtime log | `log.dir`, `log.level`, `log.rotation_size_mb`, `log.max_backup_files`, `log.format_pattern`, `log.rotation_frequency`, `log.rotation_interval`, `log.rotation_time_daily`, `log.rotation_naming_scheme`, `log.logger_name`, `log.log_filename` |
| Runtime frame | `frame.target_fps`, `frame.interval_ms`, `frame.slow_threshold_multiplier` |
| Client | `client.scripts_dir`, `client.network.server_address`, `client.network.server_port`, `client.network.reconnect_max_retries`, `client.network.reconnect_base_delay_ms`, `client.network.reconnect_max_delay_ms`, `client.network.timeout_ms`, `client.network.client_prediction`, `client.network.interpolation_delay_ms` |
| Server | `server.http.timeout_sec`, `server.scripts_dir`, `server.admin_port`, `server.admin_bind_address`, `server.admin_metrics_enabled`, `server.shutdown_timeout_sec`, `server.connection_drain_timeout_sec`, `server.max_connections`, `server.pid_file`, `server.active_mongodb`, `server.db_service`, `server.mongodb_dev`, `server.mongodb_public`, `server.db_required` |
| Server MessagePack | `server.msgpack.max_nesting_depth`, `server.msgpack.max_payload_size` |
| Server resource limits | `server.resource_limits.max_message_size`, `server.resource_limits.max_buffer_capacity`, `server.resource_limits.max_http_body_size`, `server.resource_limits.max_msgpack_depth` |
| Server TCP keepalive | `server.tcp_keepalive.idle_sec`, `server.tcp_keepalive.interval_sec`, `server.tcp_keepalive.count` |
| Server instance | `server.instance.id`, `server.instance.region`, `server.instance.zone`, `server.instance.cluster` |

### `config.get_module(name)`

Loads `resources/script/data/<name>.json` under `RuntimeConfig.resource_dir` and returns it as Lua tables. The JSON file must be a top-level array. Rows are inserted by array position and additionally indexed by numeric `id` fields when present.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `name` | `string` | `const char*` (via `luaL_checkstring`) | Data module name without `.json` |

| Returns | Type | Description |
|---------|------|-------------|
| `rows` | `table` | Array of row tables, also indexed by numeric `id` |
| `nil, err` | `nil, string` | Returned when the file is missing or not a top-level JSON array |

### `config.on_change(module, callback)`

Registers a hot-reload callback filtered by config path prefix. The callback is not called immediately by the reload thread; it is queued until `config.flush_changes()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `module` | `string` | `const char*` | Path prefix to watch. `""` or `"*"` means all changes. |
| `callback` | `function` | Lua registry ref | Signature: `function(changes)` |

| Returns | Type | Description |
|---------|------|-------------|
| `id` | `integer` | Callback id for `config.unregister(id)` |

Each `changes` item has fields `{ field = string, old_value = string, new_value = string }`.

### `config.unregister(id)`

Unregisters a callback id returned by `config.on_change()`, clears queued events for that callback, and releases the Lua registry reference owned by the same VM.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `id` | `integer` | `int` (via `luaL_checkinteger`) | Reload callback id |

| Returns | — | No return value |

### `config.flush_changes()`

Runs queued config change callbacks for the current `lua_State`.

| Returns | Type | Description |
|---------|------|-------------|
| `count` | `integer` | Number of callbacks successfully fired |

Typical usage:

```lua
local id = config.on_change("log", function(changes)
    for _, change in ipairs(changes) do
        log_info(string.format("%s: %s -> %s",
            change.field, change.old_value, change.new_value))
    end
end)

-- call once per frame or from update()
config.flush_changes()
```

## Config Files

| Config | Default Path | Description |
|--------|-------------|-------------|
| Runtime | `resources/config/runtime/runtime.json` | Engine-level settings shared by client and server |
| Client | `resources/config/client/client.json` | Client-specific settings |
| Server | `resources/config/server/server.json` | Server-specific settings |
| DB Service | `resources/config/server/db_service.json` | Database service configuration |
| MongoDB Dev | Referenced by `server.json` → `mongodb_dev` | Development MongoDB cluster |
| MongoDB Public | Referenced by `server.json` → `mongodb_public` | Production MongoDB cluster |

## Config Structs (C++)

### RuntimeConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `resource_dir` | `string` | `"resources"` | Root resource directory |
| `log` | `LogConfig` | — | Logging subsystem configuration |
| `frame` | `FrameConfig` | — | Frame/timing configuration |
| `scripts_dir` | `string` | `"resources/script/runtime"` | Runtime scripts directory |
| `sandbox_level` | `string` | `"strict"` | Lua sandbox level |
| `environment` | `string` | `"development"` | Deployment target used by environment/profile selection |

### LogConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `dir` | `string` | `"logs"` | Log output directory |
| `level` | `string` | `"info"` | Log level (trace/debug/info/warn/error/critical) |
| `rotation_size_mb` | `int` | `100` | Size-based rotation threshold (MB) |
| `max_backup_files` | `int` | `10` | Max rotated files to keep |
| `format_pattern` | `string` | — | Log message format string |
| `rotation_frequency` | `string` | `""` | Time-based rotation: "daily", "hourly", "minutely" |
| `rotation_interval` | `int` | `1` | Interval for hourly/minutely rotation |
| `rotation_time_daily` | `string` | `"00:00"` | Daily rotation time (HH:MM) |
| `rotation_naming_scheme` | `string` | `"date_and_time"` | Rotation naming: "index", "date", "date_and_time" |
| `logger_name` | `string` | — | Logger instance name |
| `log_filename` | `string` | `""` | Optional custom log filename |

### FrameConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_fps` | `int` | `30` | Target frames per second |
| `interval_ms` | `int` | `33` | Frame interval in milliseconds |
| `slow_threshold_multiplier` | `int` | `2` | Slow frame detection multiplier |

### ServerConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `http` | `HttpConfig` | — | HTTP client settings |
| `msgpack` | `MsgpackConfig` | — | MessagePack settings |
| `scripts_dir` | `string` | `"resources/script/server"` | Server scripts directory |
| `admin_port` | `int` | `8081` | Admin HTTP port; `0` disables admin HTTP |
| `admin_bind_address` | `string` | `"127.0.0.1"` | Admin HTTP bind address |
| `admin_auth_token` | `string` | `""` | Optional bearer token for admin HTTP |
| `admin_metrics_enabled` | `bool` | `true` | Expose `/metrics` when true |
| `config_webhook_url` | `string` | `""` | Optional POST target after config reload |
| `config_webhook_timeout_sec` | `int` | `5` | Config webhook timeout |
| `mongodb_dev` | `string` | — | Dev MongoDB config file path |
| `mongodb_public` | `string` | — | Production MongoDB config file path |
| `db_service` | `string` | — | Database service config file path |
| `db_required` | `bool` | `false` | Startup/readiness requires DB availability when true |
| `active_mongodb` | `string` | `""` | Explicit MongoDB selection override: `"dev"`, `"public"`, or auto |
| `shutdown_timeout_sec` | `int` | `30` | Graceful shutdown timeout |
| `connection_drain_timeout_sec` | `int` | `5` | Connection drain window during shutdown |
| `max_connections` | `int` | `10000` | Maximum concurrent TCP connections; `0` means unlimited |
| `pid_file` | `string` | `"server.pid"` | PID file path; empty disables PID file |
| `tcp_keepalive` | `TcpKeepaliveConfig` | — | TCP keepalive parameters |
| `resource_limits` | `ResourceLimits` | — | Runtime-configurable message/buffer/body limits |
| `instance` | `InstanceIdentity` | — | Multi-instance identity fields |

### HttpConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timeout_sec` | `double` | `10.0` | HTTP request timeout in seconds |

### MsgpackConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_nesting_depth` | `int` | `16` | Maximum table nesting depth for encoding |
| `max_payload_size` | `size_t` | `1048576` | Maximum MessagePack encoded payload size in bytes |

### TcpKeepaliveConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `idle_sec` | `int` | `0` | Idle seconds before keepalive probes; `0` uses OS default |
| `interval_sec` | `int` | `0` | Seconds between keepalive probes; `0` uses OS default |
| `count` | `int` | `0` | Probe count; `0` uses OS default |

### ResourceLimits

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_message_size` | `uint32_t` | `65536` | Maximum single network message size |
| `max_buffer_capacity` | `uint32_t` | `262144` | Maximum per-connection buffer capacity |
| `max_http_body_size` | `uint32_t` | `10485760` | Maximum HTTP POST body size |
| `max_msgpack_depth` | `uint32_t` | `64` | Maximum MessagePack nesting depth for encode operations |

### InstanceIdentity

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | `string` | `""` | Instance id, for example `"game-server-01"` |
| `region` | `string` | `""` | Region |
| `zone` | `string` | `""` | Zone |
| `cluster` | `string` | `""` | Cluster name |

## ConfigManager (C++)

### Loading

```
LoadRuntimeFromFile(path)     — Load runtime config from JSON file
LoadClientFromFile(path)      — Load client config from JSON file
LoadServerFromFile(path)      — Load server config from JSON file
Load(config_dir)              — Load all configs from directory tree
Reload(config_dir)            — Reload configs from disk
```

### Accessors (thread-safe)

```
GetRuntimeConfig()     → RuntimeConfig
GetClientConfig()      → ClientConfig
GetServerConfig()      → ServerConfig
```

### MongoDB Config

```
GetMongoDbDevPath()              → string
GetMongoDbPublicPath()           → string
LoadMongoDbDevConfig(out)        → bool
LoadMongoDbPublicConfig(out)     → bool
GetMongoDbDevConfig()            → const MongoDbConfig&
GetMongoDbPublicConfig()         → const MongoDbConfig&
```

## Notes

- ConfigManager is a singleton with a shared mutex for thread-safe reads
- JSON keys match struct member names (snake_case for most, camelCase for MongoDB configs)
- Lua `config.get()` exposes runtime, selected client network paths, and server scalar paths.
- Reload preserves current values on failure
