# UDP Client API (`net.udp_client`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程）。所有函数均为同步阻塞调用——`connect()` / `send()` / `do_request()` / `send_to()` 在调用线程上阻塞执行，直到操作完成或超时。 |
| **线程安全** | 是（无共享状态）。每个实例持有独立的 `evpp::udp::sync::Client`，无全局或静态可变状态。多个线程可同时使用不同的实例。静态函数 `do_request` / `send_to` 内部创建临时 Client，调用结束后即销毁。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回（或阻塞直到响应到达/超时）。 |

## Overview

The UDP client sub-module provides synchronous UDP communication. Both static (one-shot) and instance-based patterns are supported.

## Module Path

`net.udp_client`

## Static Functions

### `net.udp_client.connect(host, port)`

Creates a connected UDP client instance.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP address |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Client instance table。内部 `_ctx` 存储 light userdata (`UdpClientCtx*`) |
| `errmsg` | `string` | `lua_pushfstring` | 仅失败时返回。Error message on failure |

返回值数量：成功返回 1 个值，失败返回 2 个值 (`nil, errmsg`)。

### `net.udp_client.do_request(host, port, data [, timeout_ms])`

One-shot request/reply: sends data and waits for a response.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring` → `std::string`) | Data to send |
| `timeout_ms` | `integer` | `lua_Integer` → `uint32_t` (via `luaL_optinteger`, default 3000) | Response timeout in ms |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `response` | `string` | `lua_pushlstring` | Response data |

### `net.udp_client.send_to(host, port, data)`

One-shot fire-and-forget: sends data without waiting for a response.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `host` | `string` | `const char*` (via `luaL_checkstring`) | Remote hostname or IP |
| `port` | `integer` | `lua_Integer` → `int` (via `luaL_checkinteger`) | Remote port (1–65535) |
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Data to send |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |
| `err` | `string` | `lua_pushstring` | Error message on failure (only if `ok` is false) |

## Instance Methods

### `client:send(data)`

Sends data through the established connection.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Data to send |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `client:do_request(data [, timeout_ms])`

Sends data and waits for a response on this instance.

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

立即释放 `UdpClientCtx`（同步删除，不延迟）。

### `client:is_connected()`

Checks if the instance is connected.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `connected` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if connected |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `connect` | host | `string` | `const char*` | `luaL_checkstring` |
| `connect` | port | `integer` | `lua_Integer` → `int` | `luaL_checkinteger` + static_cast |
| `send` / `do_request` / `send_to` | data | `string` | `const char*` + `size_t` | `luaL_checklstring` |
| `do_request` | timeout_ms | `integer` | `lua_Integer` → `uint32_t` | `luaL_optinteger` |
| all send | ok | `boolean` | `int` (0/1) | `lua_pushboolean` |
| all request | response | `string` | `std::string` | `lua_pushlstring` |

## Example

```lua
-- Instance-based usage
local client = net.udp_client.connect("127.0.0.1", 5353)
if client then
    client:send("ping")
    local resp = client:do_request("query", 5000)
    log_info("Response: " .. resp)
    client:close()
end

-- Static one-shot request/reply
local resp = net.udp_client.do_request("127.0.0.1", 5353, "query", 5000)

-- Static fire-and-forget
local ok, err = net.udp_client.send_to("127.0.0.1", 5353, "event_data")
if not ok then
    log_error("send_to failed: " .. err)
end
```
