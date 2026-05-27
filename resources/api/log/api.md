# Log System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。Lua 脚本在主线程上执行，所有 log_* 函数同步调用引擎日志系统。 |
| **线程安全** | 否。必须在 ScriptVM 所属的 EventLoop 线程上调用。 |
| **回调线程** | 无回调。所有函数均为同步阻塞调用，立即返回。 |

## Overview

The log system exports six global Lua functions for logging at different severity levels. All functions take a single string argument and output it through the engine's logging infrastructure.

## Module

Global functions (no module prefix required).

## Functions

### `log_trace(msg)`

Logs a message at TRACE level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

### `log_debug(msg)`

Logs a message at DEBUG level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

### `log_info(msg)`

Logs a message at INFO level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

### `log_warn(msg)`

Logs a message at WARN level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

### `log_error(msg)`

Logs a message at ERROR level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

### `log_fatal(msg)`

Logs a message at FATAL/CRITICAL level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | The log message |

| Returns | — | 无返回值 |

## 参数类型详述

| 函数 | 参数 | Lua 类型 | C 类型 | 说明 |
|------|------|----------|--------|------|
| 所有函数 | `msg` | `string` | `const char*` | 通过 `luaL_checkstring` 获取，`nil` 或非字符串会抛出 Lua error。消息在日志输出中自动添加 `[lua]` 前缀。 |

## Example

```lua
log_info("Server started successfully")
log_warn("Connection pool running low: " .. remaining)
log_error("Failed to parse incoming message: " .. err)
```

## Notes

- All messages are prefixed with `[lua]` in the log output
- Functions are registered directly as globals (not under a module table)
