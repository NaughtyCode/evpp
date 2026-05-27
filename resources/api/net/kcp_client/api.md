# KCP Client API (`net.kcp_client`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程）。所有函数均为同步阻塞调用——`connect()` / `send()` / `do_request()` 在调用线程上阻塞执行，直到操作完成或超时。 |
| **线程安全** | 是（无共享状态）。每个实例持有独立的 `evpp::kcp::sync::Client`，无全局或静态可变状态。多个线程可同时使用不同的实例。静态函数 `do_request` 内部创建临时 Client，调用结束后即销毁。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回（或阻塞直到响应到达/超时）。 |

## Overview

The KCP client sub-module provides reliable UDP communication using the KCP (KCP - A Fast and Reliable ARQ Protocol) protocol. Supports both instance-based (with tuning) and static one-shot usage.

## Module Path

`net.kcp_client`

## Static Functions

### `net.kcp_client.new([conv])`

Creates an unconnected KCP client instance. Useful when KCP tuning is needed before connecting.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `conv` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_checkinteger`) | Conversation ID (0 to UINT32_MAX, default: `0x11223344`) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Unconnected client instance。内部 `_ctx` 存储 light userdata (`KcpClientCtx*`) |

### `net.kcp_client.connect(host, port [, conv])`

Creates and connects a KCP client in one step.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |
| `conv` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_optinteger`, default `0x11223344`) | Conversation ID |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Connected client instance |
| `errmsg` | `string` | `lua_pushfstring` | Error message on failure (仅失败时) |

返回值数量：成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

### `net.kcp_client.do_request(host, port, data [, timeout_ms [, conv]])`

One-shot KCP request/reply.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring` → `std::string`) | Data to send |
| `timeout_ms` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_optinteger`, default 3000) | Response timeout in ms |
| `conv` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_optinteger`, default `0x11223344`) | Conversation ID |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `response` | `string` | `lua_pushlstring` | Response data |

## Instance Methods

### `client:connect(host, port)`

Connects a previously-unconnected instance (created via `new()`). Any KCP tuning set before this call takes effect.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |
| `err` | `string` | `lua_pushfstring` | Error message on failure |

### `client:send(data)`

Sends data through the KCP connection.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Data to send |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `client:do_request(data [, timeout_ms])`

Sends data and waits for a response.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring` → `std::string`) | Data to send |
| `timeout_ms` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_optinteger`, default 3000) | Response timeout in ms |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `response` | `string` | `lua_pushlstring` | Response data |

### `client:close()`

Closes the connection and releases resources.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already closed |

立即释放 `KcpClientCtx`（同步删除）。

### `client:is_connected()`

Checks if the client is connected.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `connected` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if connected |

### `client:set_kcp_nodelay(nodelay, interval, resend, nc)`

Configures KCP nodelay mode. Must be called before `connect()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `nodelay` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | 0=disable, 1=enable nodelay |
| `interval` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Internal update interval (ms) |
| `resend` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Fast resend threshold (0=disable) |
| `nc` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | No congestion window (0=disable, 1=enable) |

| Returns | — | 无返回值 |

### `client:set_kcp_wnd_size(sndwnd, rcvwnd)`

Configures KCP window sizes. Must be called before `connect()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `sndwnd` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Send window size |
| `rcvwnd` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Receive window size |

| Returns | — | 无返回值 |

### `client:set_kcp_mtu(mtu)`

Configures KCP MTU. Must be called before `connect()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `mtu` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Maximum transmission unit |

| Returns | — | 无返回值 |

### `client:set_kcp_conv(conv)`

Sets the conversation ID. Must be called before `connect()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `conv` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_checkinteger`) | Conversation ID (0 to UINT32_MAX) |

| Returns | — | 无返回值 |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `connect` / `new` | host | `string` | `const char*` | `luaL_checkstring` |
| `connect` / `new` | port | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` + static_cast |
| `connect` / `new` / `set_kcp_conv` | conv | `integer` | `lua_Integer` → `uint32_t` | `luaL_checkinteger` / `luaL_optinteger` |
| `send` / `do_request` | data | `string` | `const char*` + `size_t` | `luaL_checklstring` |
| `do_request` | timeout_ms | `integer` | `lua_Integer` → `uint32_t` | `luaL_optinteger` |
| all KCP tuning | nodelay, interval, etc. | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` |

## Example

```lua
-- Simple connect + request
local client = net.kcp_client.connect("127.0.0.1", 9000, 0x1234)
if client then
    client:send("hello")
    local resp = client:do_request("ping", 5000)
    log_info("Response: " .. resp)
    client:close()
end

-- Tuned connection via new()
local client = net.kcp_client.new(0x5678)
client:set_kcp_nodelay(1, 10, 2, 1)
client:set_kcp_wnd_size(128, 128)
client:set_kcp_mtu(512)
local ok, err = client:connect("127.0.0.1", 9000)
if ok then
    client:send("tuned hello")
    client:close()
end

-- One-shot static
local resp = net.kcp_client.do_request("127.0.0.1", 9000, "query", 5000, 0x9ABC)
```
