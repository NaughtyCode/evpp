# UDP Server API (`net.udp_server`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`listen()` / `stop()` / `pause()` / `continue()` / `is_running()` / `set_on_message()` 必须在 ScriptVM 所属的主线程上调用。`stop(true)` 会阻塞调用线程直到所有 recv 线程退出。 |
| **线程安全** | 否。`UdpServerCtx` 通过 light userdata 存储在 Lua 实例表中。`on_message_ref` 使用 `std::atomic<int>` 实现跨线程安全读取（recv 线程读取、主线程写入），但实例本身不可跨线程共享。 |
| **回调线程** | 主线程（EventLoop 线程）。消息在 recv 线程接收后，通过 `RunInLoop` 将数据和回调派发到主线程执行。数据在派发前已拷贝到 `std::string`，避免 buffer 复用竞争。 |

## Overview

The UDP server sub-module provides asynchronous UDP message reception. Messages arrive on worker threads and are dispatched to the main EventLoop thread before invoking Lua callbacks.

## Module Path

`net.udp_server`

## Static Functions

### `net.udp_server.listen(port, on_message)`

Creates and starts a UDP server.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `port` | `integer` or `string` | `lua_Integer` → `int` or `const char*` (via `lua_type` + `luaL_checkinteger` / `luaL_checkstring`) | Single port number (1–65535) or comma-separated port string (e.g. `"5353"` or `"53,5353"`) |
| `on_message` | `function` | `int` (Lua registry ref via `luaL_ref`) | Callback for received messages. Signature: `function(data, remote_ip)` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Server instance table。内部 `_ctx` 存储 light userdata (`UdpServerCtx*`) |
| `errmsg` | `string` | `lua_pushstring` | 仅失败时返回 |

返回值数量：成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

## Server Instance Methods

### `server:stop()`

Stops the server and releases resources. Blocks until all receive threads exit.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already stopped |

UdpServerCtx 和 registry ref 通过 `RunInLoop` 延迟释放（确保已排队的 message callback 先执行完毕）。

### `server:pause()`

Pauses message reception. Messages received while paused are queued by the OS.

| Returns | — | 无返回值 |

### `server:continue()`

Resumes message reception after a pause.

| Returns | — | 无返回值 |

### `server:is_running()`

Checks if the server is running.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `running` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if active |

### `server:set_on_message(callback)`

Replaces the message callback. The old callback is deferred-unref'd to allow pending tasks to complete.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | `int` (Lua registry ref via `luaL_ref`)。旧 ref 原子交换为 `LUA_NOREF` 后通过 `RunInLoop` 延迟 unref。 | Signature: `function(data, remote_ip)` |

## Callback Signature

```
on_message(data, remote_ip)
```
- `data`: `string` — Received message payload。C 类型：`const char*` + `size_t`（`lua_pushlstring`）
- `remote_ip`: `string` — Sender's IP address。C 类型：`const char*` + `size_t`（`lua_pushlstring`）

The callback is invoked on the main EventLoop thread, not the receive thread. Data is copied before dispatch to avoid buffer reuse races.

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `listen` | port (number) | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` + static_cast |
| `listen` | port (string) | `string` | `const char*` | `luaL_checkstring` |
| `listen` | on_message | `function` | `int` (registry ref) | `luaL_ref` |
| callback | data | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |
| callback | remote_ip | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |

## Example

```lua
local server = net.udp_server.listen(5353, function(data, remote_ip)
    log_info("Received from " .. remote_ip .. ": " .. data)
end)

-- Later: change the handler
server:set_on_message(function(data, remote_ip)
    log_info("New handler: " .. data)
end)

-- Pause/resume
server:pause()
-- ... do something ...
server:continue()

-- Check status
if server:is_running() then
    log_info("UDP server is running")
end

-- Graceful shutdown
server:stop()
```

## Lifecycle

- All active UDP servers are tracked and stopped during engine shutdown via `ShutdownUdpServerBindings()`
- `Stop(true)` waits for all receive threads to exit before returning
- Deferred cleanup: refs are unref'd and ctx is deleted via `RunInLoop` to allow any queued message callbacks to complete first
