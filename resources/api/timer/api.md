# Timer System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`timer.timeout()` / `timer.interval()` / `timer.cancel()` 必须在 ScriptVM 所属线程上调用。 |
| **线程安全** | 否。定时器状态（`TimerBindState`）存储在 Lua registry 中，与 ScriptVM 绑定，不可跨线程访问。 |
| **回调线程** | 主线程（EventLoop 线程）。定时器回调通过 `TimerManager` 在 EventLoop 上触发，与 Lua 脚本执行在同一线程。 |

## Overview

The timer system provides one-shot and repeating timers driven by the engine's high-resolution timer manager. Timer state is per-ScriptVM — each VM has an independent timer table.

## Module

`timer`

## Functions

### `timer.timeout(delay_ms, callback)`

Creates a one-shot timer that fires once after `delay_ms` milliseconds.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `delay_ms` | `integer` | `int64_t` | Delay in milliseconds (1 ~ INT64_MAX/1000000)。必须为正整数。 |
| `callback` | `function` | `lua_CFunction` (via registry ref) | Lua function to call when the timer fires. Receives no arguments. |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `timer_id` | `integer` | `lua_Integer` (→ `TimerId`) | Unique timer identifier for use with `timer.cancel()` |

失败时抛出 Lua error（延迟越界、回调非函数、定时器系统未初始化、创建失败）。

### `timer.interval(period_ms, callback)`

Creates a repeating timer that fires every `period_ms` milliseconds.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `period_ms` | `integer` | `int64_t` | Interval in milliseconds (1 ~ INT64_MAX/1000000)。必须为正整数。 |
| `callback` | `function` | `lua_CFunction` (via registry ref) | Lua function to call on each tick. Receives no arguments. |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `timer_id` | `integer` | `lua_Integer` (→ `TimerId`) | Unique timer identifier |

### `timer.cancel(timer_id)`

Cancels an active timer. The callback will not fire after cancellation.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `timer_id` | `integer` | `TimerId` (uint64) | The timer ID returned by `timeout()` or `interval()` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| result | `boolean` | `int` (0/1) | `true` on success |
| err | `nil` + `string` | — | `nil` + error message on failure (timer not found or system not initialized) |

返回值数量: 成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

## 回调签名

```
callback()   -- 无参数，无返回值
```

**注意**：回调中如果调用 `timer:cancel()` 取消自身定时器，取消逻辑是安全的（通过 shared_ptr keep-alive 和 ref == LUA_NOREF 检测）。

## Example

```lua
-- One-shot: log after 5 seconds
local tid = timer.timeout(5000, function()
    log_info("5 seconds elapsed")
end)

-- Repeating: heartbeat every 1 second
local heartbeat_id = timer.interval(1000, function()
    log_debug("heartbeat")
end)

-- Cancel the heartbeat after 10 seconds
timer.timeout(10000, function()
    timer.cancel(heartbeat_id)
    log_info("heartbeat stopped")
end)
```

## Lifecycle

- Timers are automatically cancelled when `ShutdownTimerBindings()` is called during engine shutdown
- Each `ScriptVM` has its own independent timer state
- A cancelled timer's callback will never fire, even if already queued
