# TCP Server API (`net.server`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`listen()` / `stop()` / `set_on_*` 以及所有 conn 方法（`send` / `close` / `is_connected` / `set_on_*`）必须在 ScriptVM 所属的主线程上调用。 |
| **线程安全** | 否。`ServerCtx` 和 `ConnCtx` 通过 light userdata 存储在 Lua 实例表中，与 ScriptVM 绑定。`g_server_ctxs` 仅用于 shutdown 时遍历。 |
| **回调线程** | 主线程（EventLoop 线程）。连接/断开/消息事件通过 evpp `TCPServer` 在 EventLoop 上触发，回调通过 `lua_pcall` 直接调用。 |

## Overview

The TCP server sub-module provides asynchronous TCP server functionality. It creates a server instance that accepts connections, each represented by a connection instance with its own send/receive methods.

## Module Path

`net.server`

## Static Functions

### `net.server.listen(addr)`

Creates and starts a TCP server listening on the specified address.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `addr` | `string` | `const char*` (via `luaL_checkstring`) | Address in `"host:port"` format (e.g. `"0.0.0.0:8080"`) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `instance` | `table` | Lua table (via `lua_newtable`) | Server instance table with methods。内部 `_ctx` 存储 light userdata (`ServerCtx*`)。 |

Errors if init or start fails (e.g. port already in use).

## Server Instance Methods

### `server:stop()`

Stops the server and releases resources. All active connections are closed.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already stopped |

`ServerCtx` 通过 `QueueInLoop` 延迟删除（确保所有 `ConnCtx` 的 `HandleClose` 先执行完毕）。

### `server:set_on_connect(callback)`

Sets or clears the new-connection callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when a new client connects. Signature: `function(self, conn, remote_addr)`。`conn` 为 connection 实例表，`remote_addr` 为 `string`（`lua_pushlstring`）。 |

### `server:set_on_message(callback)`

Sets the server-wide message callback (used when a connection has no per-connection handler).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when data arrives on a connection. Signature: `function(self, conn, data)`。`data` 为 `string`（`lua_pushlstring`）。 |

### `server:set_on_close(callback)`

Sets the server-wide close callback (used when a connection has no per-connection handler).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Called when a client disconnects. Signature: `function(self, conn, remote_addr)` |

## Connection Instance Methods

Connection instances are passed to server callbacks. Each connection has its own methods and can override server-wide callbacks. 内部 `_ctx` 存储 light userdata (`ConnCtx*`)。

### `conn:send(data)`

Sends data to this client.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | Binary data to send |

| Returns | — | 无返回值。错误时抛出 Lua error。 |

### `conn:close()`

Closes this connection.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already closed |

### `conn:is_connected()`

Checks if the connection is still established.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `connected` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if connected |

### `conn:set_on_message(callback)`

Sets a per-connection message callback (overrides server-wide).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Signature: `function(self, data)` |

### `conn:set_on_close(callback)`

Sets a per-connection close callback (overrides server-wide).

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` or `nil` | Lua value (stored via `lua_setfield` on instance table) | Signature: `function(self, remote_addr)` |

## Callback Signatures

**Server-level:**
```
on_connect(self, conn, remote_addr)    -- conn: connection instance table, remote_addr: string
on_message(self, conn, data)           -- data: string (const char*, size_t → lua_pushlstring)
on_close(self, conn, remote_addr)
```

**Connection-level (override):**
```
on_message(self, data)                 -- self is the connection instance
on_close(self, remote_addr)
```

Per-connection callbacks take priority over server-wide callbacks. If a connection has `on_message` set, the server's `on_message` will not be called for that connection.

## Example

```lua
local server = net.server.listen("0.0.0.0:9000")

server:set_on_connect(function(self, conn, addr)
    log_info("New connection from " .. addr)

    -- Set per-connection message handler
    conn:set_on_message(function(conn_self, data)
        log_info("Received: " .. data)
        conn_self:send("Echo: " .. data)
    end)

    conn:set_on_close(function(conn_self, addr)
        log_info("Client disconnected: " .. addr)
    end)
end)

server:set_on_close(function(self, conn, addr)
    log_info("Client disconnected (server handler): " .. addr)
end)

-- Later:
server:stop()
```

## Lifecycle

- All active servers are tracked and stopped during engine shutdown via `ShutdownServerBindings()`
- Connection instances are reference-counted and cleaned up automatically on disconnect
- Thread count is 0 (all connections handled on the main EventLoop thread)
