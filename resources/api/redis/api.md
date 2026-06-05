# Redis API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 拥有当前 `ScriptVM` 的线程。Lua API 本身不可跨 VM 或跨线程直接调用。 |
| **线程安全** | `RedisClient` C++ 单例是线程安全的；Lua binding 只负责在当前 VM 注册回调并提交请求。 |
| **回调线程** | Redis I/O 在 `RedisClientThread` 中执行；Lua callback 只通过当前 VM 的 `AsyncResultDispatcher` 回到提交请求的 owner 线程执行。 |

## Overview

The Redis module provides asynchronous Redis commands for Lua scripts. It is
available only when the target is built with `ENGINE_REDIS_ENABLED`; client and
mobile targets do not compile `src/runtime/database/redis`.

## Module

`redis` (global table and `require("redis")`)

## Lua Functions

### `redis.command(argv, callback[, options])`

Submits one Redis command.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `argv` | `table` | `std::vector<std::string>` | Array of command tokens. The first token is the command name. |
| `callback` | `function` | Lua registry ref | Called once when an accepted request completes. |
| `options` | `table` or `nil` | `RedisCommandOptions` | Optional fields: `timeout_ms`, `routing_key`. |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `boolean` | `true` when the request was accepted. |
| `request_id_or_error` | `integer` or `string` | Request id on success, error message on synchronous rejection. |

### `redis.eval(script, keys, args, callback[, options])`

Submits an `EVAL` command.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `script` | `string` | `std::string` | Lua script executed by Redis. |
| `keys` | `table` | `std::vector<std::string>` | Redis `KEYS`; must be an array of strings. |
| `args` | `table` | `std::vector<std::string>` | Redis `ARGV`; must be an array of strings. |
| `callback` | `function` | Lua registry ref | Called once when an accepted request completes. |
| `options` | `table` or `nil` | `RedisCommandOptions` | Optional fields: `timeout_ms`, `routing_key`. |

If `options.routing_key` is omitted and `keys[1]` exists, the first key is used
as the routing key so related eval requests keep worker-local ordering.

### `redis.is_running()`

| Returns | Type | Description |
|---------|------|-------------|
| `running` | `boolean` | `true` if `RedisClient` has running worker threads. |

### `redis.is_healthy()`

| Returns | Type | Description |
|---------|------|-------------|
| `healthy` | `boolean` | `true` if all Redis workers are healthy. |

### `redis.dispatch([max])`

Dispatches queued Redis callbacks for the current VM.

| Parameter | Type | Description |
|-----------|------|-------------|
| `max` | `integer` or `nil` | Maximum callbacks to dispatch. Defaults to the binding context batch size. |

| Returns | Type | Description |
|---------|------|-------------|
| `count` | `integer` | Number of callbacks dispatched. |

Most engine owner loops call `ScriptVM::DispatchAsyncResults()` before
`UpdateScript()`, so scripts usually do not need to call `redis.dispatch()`
manually.

## Options Table

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timeout_ms` | `integer` | `0` | `0` uses `redis.connection.command_timeout_ms`. |
| `routing_key` | `string` | `""` | Stable per-key worker routing for ordering. |

## Callback Result

Accepted requests call `callback(result)` with:

| Field | Type | Description |
|-------|------|-------------|
| `status` | `string` | `ok`, `command_error`, `connection_error`, `auth_error`, `protocol_error`, `timeout`, `shutdown`, or `dropped`. |
| `ok` | `boolean` | `true` only when `status == "ok"`. |
| `request_id` | `integer` | Request id returned by the submit call. |
| `error` | `string` or absent | Error message when available. |
| `value` | `table` | Redis value wrapper. |

Redis value wrapper:

| Field | Type | Description |
|-------|------|-------------|
| `type` | `string` | `null`, `string`, `status`, `error`, `integer`, `double`, `bool`, `array`, `map`, `set`, `push`, `attribute`, `bignumber`, `verbatim_string`, or `unknown`. |
| `value` | varies | Scalar value, `nil`, or an array of nested Redis value wrappers. |

## Unsupported Commands

Normal Lua submissions reject commands that change connection state, block a
worker, enter Pub/Sub mode, or start transactions. Examples include `AUTH`,
`HELLO`, `SELECT`, `QUIT`, `RESET`, `CLIENT`, `MONITOR`, `SUBSCRIBE`, `MULTI`,
`EXEC`, `WATCH`, `UNWATCH`, `WAIT`, `WAITAOF`, blocking `B*` commands, and
`XREAD` / `XREADGROUP` with `BLOCK`.

Authentication and database selection are handled only by `RedisClientThread`
from `resources/config/server/redis.json`.

## Example

```lua
local ok, request_id = redis.command({ "SET", "player:1", "online" }, function(result)
    if not result.ok then
        log_warn("redis SET failed: " .. tostring(result.error))
    end
end, { routing_key = "player:1" })

if not ok then
    log_error("redis submit failed: " .. tostring(request_id))
end

redis.command({ "GET", "player:1" }, function(result)
    if result.ok and result.value.type == "string" then
        log_info("player:1=" .. result.value.value)
    end
end, { timeout_ms = 1000, routing_key = "player:1" })
```
