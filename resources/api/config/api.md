# Config System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 任意线程。`ConfigManager` 使用 `std::shared_mutex` 保护读取——读取操作可在多线程并发调用，加载/重载需独占写锁。 |
| **线程安全** | 是。读操作（`Get*Config()`）使用 `std::shared_lock`（多读者并发安全），写操作（`Load*FromFile()` / `Reload()`）使用 `std::unique_lock`（排他写）。访问器的返回值是值拷贝，不持有锁。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。 |

## Overview

The Config system manages all engine configuration loaded from JSON files at startup. It provides thread-safe access to runtime, client, server, and MongoDB cluster configurations.

## Module

C++ API via `ConfigManager` singleton. Not directly exposed to Lua — configuration values are read by native code and made available through other API modules.

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
| `scripts_dir` | `string` | — | Runtime scripts directory |

### LogConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `dir` | `string` | `"logs"` | Log output directory |
| `level` | `string` | `"info"` | Log level (trace/debug/info/warn/error/critical) |
| `rotation_size_mb` | `int` | `10` | Size-based rotation threshold (MB) |
| `max_backup_files` | `int` | `5` | Max rotated files to keep |
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
| `slow_threshold_multiplier` | `int` | `3` | Slow frame detection multiplier |

### ServerConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `http` | `HttpConfig` | — | HTTP client settings |
| `msgpack` | `MsgpackConfig` | — | MessagePack settings |
| `scripts_dir` | `string` | — | Server scripts directory |
| `mongodb_dev` | `string` | — | Dev MongoDB config file path |
| `mongodb_public` | `string` | — | Production MongoDB config file path |
| `db_service` | `string` | — | Database service config file path |

### HttpConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timeout_sec` | `double` | `5.0` | HTTP request timeout in seconds |

### MsgpackConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_nesting_depth` | `int` | — | Maximum table nesting depth for encoding |

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
- Reload preserves current values on failure
