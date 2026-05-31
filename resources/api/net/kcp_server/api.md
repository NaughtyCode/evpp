# KCP Server API (`net.kcp_server`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`listen()` / `stop()` / `pause()` / `continue()` / `is_running()` / `set_on_message()` / KCP 调参方法必须在 ScriptVM 所属的主线程上调用。`stop(true)` 会阻塞调用线程直到所有 recv 线程退出。 |
| **线程安全** | 否。`KcpServerCtx` 通过 light userdata 存储在 Lua 实例表中。`on_message_ref` 使用 `std::atomic<int>` 实现跨线程安全读取（recv 线程读取、主线程写入），但实例本身不可跨线程共享。 |
| **回调线程** | 主线程（EventLoop 线程）。消息在 recv 线程接收后，通过 `RunInLoop` 将数据和回调派发到主线程执行。数据在派发前已拷贝到 `std::string`，避免 buffer 复用竞争。 |

## Overview

The KCP server sub-module provides reliable UDP message reception using the KCP protocol. Messages arrive on worker threads and are dispatched to the main EventLoop thread before invoking Lua callbacks. Each message includes the remote IP, data, and KCP conversation ID.

## Module Path

`net.kcp_server`

## Static Functions

### `net.kcp_server.listen(port, on_message)`

Creates and starts a KCP server.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `port` | `integer` or `string` | `lua_Integer` → `int` or `const char*` (via `lua_type` + `luaL_checkinteger` / `luaL_checkstring`) | Single port number (1–65535) or comma-separated port string |
| `on_message` | `function` | `int` (Lua registry ref via `luaL_ref`) | Callback for received messages. Signature: `function(data, remote_ip, conv)` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Server instance table。内部 `_ctx` 存储 light userdata (`KcpServerCtx*`) |
| `errmsg` | `string` | `lua_pushstring` | 仅失败时返回 |

返回值数量：成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

## Server Instance Methods

### `server:stop()`

Stops the server and releases resources. Blocks until all receive threads exit.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already stopped |

### `server:pause()`

Pauses message reception.

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

Replaces the message callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | `int` (Lua registry ref via `luaL_ref`)。旧 ref 原子交换为 `LUA_NOREF` 后通过 `RunInLoop` 延迟 unref。 | Signature: `function(data, remote_ip, conv)` |

### `server:set_kcp_nodelay(nodelay, interval, resend, nc)`

Configures KCP nodelay mode for all sessions.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `nodelay` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | 0=disable, 1=enable |
| `interval` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Internal update interval (ms) |
| `resend` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Fast resend threshold (0=disable) |
| `nc` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | No congestion window (0=disable, 1=enable) |

| Returns | — | 无返回值 |

### `server:set_kcp_wnd_size(sndwnd, rcvwnd)`

Configures KCP window sizes for all sessions.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `sndwnd` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Send window size |
| `rcvwnd` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Receive window size |

| Returns | — | 无返回值 |

### `server:set_kcp_mtu(mtu)`

Configures KCP MTU for all sessions.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `mtu` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Maximum transmission unit |

| Returns | — | 无返回值 |

### `server:set_session_timeout(timeout_ms)`

Sets the session timeout. Inactive sessions are cleaned up after this duration.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `timeout_ms` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_checkinteger`) | Timeout in milliseconds (0 to UINT32_MAX) |

| Returns | — | 无返回值 |

### `server:set_max_message_size(max_bytes)`

Sets the per-message receive size limit for all KCP sessions on this server.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `max_bytes` | `integer` | `lua_Integer` → `size_t` (via `luaL_checkinteger`) | Maximum accepted message payload size. Must be greater than 0. |

| Returns | — | 无返回值 |

Invalid values raise a Lua error.

## Callback Signature

```
on_message(data, remote_ip, conv)
```
- `data`: `string` — Received message payload。C 类型：`const char*` + `size_t`（`lua_pushlstring`）
- `remote_ip`: `string` — Sender's IP address。C 类型：`const char*` + `size_t`（`lua_pushlstring`）
- `conv`: `integer` — KCP conversation ID。C 类型：`lua_Integer`（`lua_pushinteger`）

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `listen` | port (number) | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` + static_cast |
| `listen` | port (string) | `string` | `const char*` | `luaL_checkstring` |
| `listen` | on_message | `function` | `int` (registry ref) | `luaL_ref` |
| callback | data | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |
| callback | remote_ip | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |
| callback | conv | `integer` | `uint32_t` → `lua_Integer` | `lua_pushinteger` |
| KCP tuning | nodelay, interval, etc. | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` |
| `set_session_timeout` | timeout_ms | `integer` | `lua_Integer` → `uint32_t` | `luaL_checkinteger` |
| `set_max_message_size` | max_bytes | `integer` | `lua_Integer` → `size_t` | `luaL_checkinteger` |

## Example

```lua
local server = net.kcp_server.listen(9000, function(data, remote_ip, conv)
    log_info(string.format("KCP from %s conv=%d: %s", remote_ip, conv, data))
end)

-- Configure KCP parameters
server:set_kcp_nodelay(1, 10, 2, 1)
server:set_kcp_wnd_size(256, 256)
server:set_kcp_mtu(1400)
server:set_session_timeout(30000)
server:set_max_message_size(1024 * 1024)

-- Pause/resume
server:pause()
server:continue()

-- Graceful shutdown
server:stop()
```

## Lifecycle

- All active KCP servers are tracked and stopped during engine shutdown via `ShutdownKcpServerBindings()`
- Same deferred cleanup pattern as UDP server
