# TCP Client API (`net.client`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`connect()` 在调用线程上创建 TCP 连接，`send()` / `disconnect()` / `is_connected()` / `set_on_*` 必须在 ScriptVM 所属的主线程上调用。 |
| **线程安全** | 否。`ClientCtx`（light userdata + Lua 实例表）存储在 Lua registry 中，与 ScriptVM 绑定，不可跨 VM 或跨线程共享。回调通过 `lua_pcall` 在实例表上直接调用。 |
| **回调线程** | 主线程（EventLoop 线程）。TCP 连接/消息/断开事件通过 evpp `TCPClient` 在 EventLoop 上触发，与 Lua 脚本执行在同一线程。 |

## Overview

The TCP client sub-module provides asynchronous TCP connections to remote servers. It creates a Lua class instance with methods for sending data, managing connection lifecycle, and handling events via callbacks.

## Module Path

`net.client`

## Static Functions

### `net.client.connect(addr)`

Creates a new TCP connection to the specified address.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `addr` | `string` | `const char*` (via `luaL_checkstring`) | Address in `"host:port"` format (e.g. `"127.0.0.1:8080"`) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Client instance table with methods。内部 `_ctx` 字段存储 light userdata (`ClientCtx*`)。 |

Errors if the address is empty or the EventLoop is unavailable.

## Instance Methods

### `client:send(data)`

Sends data through the connection.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Binary data to send |

| Returns | — | 无返回值。错误时抛出 Lua error。 |

Errors if the client is closed or not connected.

### `client:disconnect()`

Closes the connection and releases resources.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already closed |

After calling `disconnect()`, the instance is invalid and should not be used. `ClientCtx` 通过 `RunInLoop` 延迟删除。

### `client:is_connected()`

Checks the current connection status.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `connected` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the underlying TCP connection is established |

### `client:set_on_connect(callback)`

Sets or clears the connection-established callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when the connection is established. Signature: `function(self)` |

### `client:set_on_message(callback)`

Sets or clears the data-received callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when data arrives. Signature: `function(self, data)` where `data` is a `string` (`lua_pushlstring`) |

### `client:set_on_close(callback)`

Sets or clears the connection-closed callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when the connection closes. Signature: `function(self)` |

## Callback Signatures

```
on_connect(self)
on_message(self, data)    -- data: string (const char*, size_t → lua_pushlstring)
on_close(self)
```

## Example

```lua
local client = net.client.connect("127.0.0.1:9000")

client:set_on_connect(function(self)
    log_info("Connected to server")
    self:send("Hello, server!")
end)

client:set_on_message(function(self, data)
    log_info("Received: " .. data)
    self:send("Echo: " .. data)
end)

client:set_on_close(function(self)
    log_info("Connection closed")
end)

-- Later:
if client:is_connected() then
    client:send("ping")
end

-- Cleanup:
client:disconnect()
```

## Lifecycle

- Auto-reconnect is disabled
- The instance is garbage-collected via `__gc` metamethod if not explicitly disconnected
- Disconnection clears callbacks and defers `ClientCtx` deletion to the EventLoop
