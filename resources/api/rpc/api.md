# RPC System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`rpc.new_server()` / `rpc.new_client()` 以及所有实例方法必须在 ScriptVM 所属的主线程上调用。 |
| **线程安全** | 否。`RpcServerCtx` / `RpcClientCtx` 通过 light userdata 存储在 Lua 实例表中，与 ScriptVM 绑定。服务端请求通过 `PendingRpcCall` 队列从 transport 线程传送到主线程；客户端异步响应通过 `deferred_responses` 队列从任意线程传送到主线程。共享所有权通过 `shared_ptr` + `weak_ptr` 保证 transport 线程回调中的安全访问。 |
| **回调线程** | 主线程（EventLoop 线程）。`UpdateRpcBindings()` 每帧在主线程上排空 pending 队列和 deferred response 队列，通过 `lua_pcall` 调用 Lua 回调（服务端 service handler / 客户端 send callback / call_async callback）。transport 线程仅负责将请求/响应推入队列，不直接操作 Lua state。 |

## Overview

The RPC system provides a client-server RPC framework exposed to Lua. Servers register named services with Lua handler callbacks; clients invoke remote methods either synchronously (`call`) or asynchronously (`call_async`). Transport is abstracted — the Lua caller provides a send callback (`set_send_callback`) that the RPC client invokes to deliver wire-format messages.

## Module

`rpc`

## Functions

### `rpc.new_server()`

Creates a new RPC server instance.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `server` | `table` | Lua table (via `PushInstanceTable`) | Server instance table with methods。内部 `_ctx` 存储 light userdata (`RpcServerCtx*`)。 |

Errors if the RPC bind state is not initialized.

### `rpc.new_client()`

Creates a new RPC client instance.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `client` | `table` | Lua table (via `PushInstanceTable`) | Client instance table with methods。内部 `_ctx` 存储 light userdata (`RpcClientCtx*`)。 |

---

## Server Instance Methods

### `server:register_service(service_name, callback)`

Registers a named RPC service with a Lua handler callback.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `service_name` | `string` | `const char*` (via `luaL_checkstring`) | Service name to register。客户端通过此名称调用远程方法。 |
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Handler callback. Signature: `function(service_name, method_name, body_json) → response_json` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

**Callback signature:**

```
function(service_name, method_name, body_json)
    -- service_name: string — 服务名
    -- method_name: string — 方法名
    -- body_json:   string — 请求体（JSON 字符串）
    return response_json  -- string — 响应体（JSON 字符串），非字符串返回值视为 "{}"
end
```

处理超时：handler 回调需在 transport 层设置的超时（默认 5 秒）内返回。超时 transport 线程自动返回 `{"error":"rpc handler timed out"}` 给调用方。

重新注册同一 service_name 会释放旧回调 ref 并替换为新回调。

### `server:unregister_service(service_name)`

Unregisters a previously registered RPC service.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `service_name` | `string` | `const char*` (via `luaL_checkstring`) | Service name to unregister |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

释放注册的回调 ref。底层 transport 层同步取消注册。

### `server:stop()`

Stops the server and releases all resources. All pending RPC calls are drained with an error response.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already stopped |

停止流程：
1. 设置 `disposed = true` 和 `alive = false`
2. 释放所有 service callback ref
3. 排空 pending 队列（所有阻塞的 transport 线程 promise 收到 `{"error":"server stopped"}`）
4. 销毁底层 `RpcServer`
5. 清除 `_ctx` 字段、释放 instance ref、移除 shared_ptr

---

## Client Instance Methods

### `client:call(service, method, args [, timeout_ms])`

Synchronous RPC call. Blocks the calling thread until a response is received or timeout.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `service` | `string` | `const char*` (via `luaL_checkstring`) | Remote service name |
| `method` | `string` | `const char*` (via `luaL_checkstring`) | Remote method name |
| `args` | `string` | `const char*` (via `luaL_optstring`, default `"{}"`) | Request body (JSON string) |
| `timeout_ms` | `integer` | `lua_Integer` → `int` (via `luaL_optinteger`, default 5000) | Timeout in milliseconds |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `body` | `string` | `lua_pushstring` | Response body (JSON string)。成功时 |
| `err` | `nil` + `string` | — | `nil` + error message on failure |

返回值数量：成功返回 2 个值（body, nil），失败返回 2 个值（nil, errmsg）。

必须先调用 `client:set_send_callback()` 设置传输层回调，否则返回错误 `"client has no transport"`。

### `client:call_async(service, method, args, callback)`

Asynchronous RPC call. Returns immediately; callback is invoked on the main thread when the response arrives.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `service` | `string` | `const char*` (via `luaL_checkstring`) | Remote service name |
| `method` | `string` | `const char*` (via `luaL_checkstring`) | Remote method name |
| `args` | `string` | `const char*` (via `luaL_optstring`, default `"{}"`) | Request body (JSON string) |
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Called on main thread when response arrives. Signature: `function(body, err)` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the request was dispatched |

**Callback signature:**

```
function(body, err)
    -- body: string or nil — 响应体（成功时）
    -- err:  nil or string — 错误消息（失败时）
end
```

响应从 transport 线程通过 `deferred_responses` 队列传到主线程，由 `UpdateRpcBindings()` 排空并调用 Lua 回调。

### `client:set_send_callback(callback)`

Sets the transport-layer send callback. This is **required** before calling `call()` or `call_async()`.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Called when the RPC client needs to send a wire-format message. Signature: `function(msgid, service, method, body)` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

**Callback signature:**

```
function(msgid, service, method, body)
    -- msgid:   integer — 消息 ID（用于匹配响应）
    -- service: string  — 目标服务名
    -- method:  string  — 目标方法名
    -- body:    string  — 请求体（JSON 字符串）
end
```

重新调用会释放旧回调 ref 并替换。

### `client:stop()`

Stops the client and releases all resources. Drains pending response callbacks without invoking them.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success, `false` if already stopped |

停止流程：
1. 设置 `disposed = true`
2. 释放 send callback ref
3. 销毁底层 `RpcClient`（析构函数会 fulfill 所有 pending promise）
4. 排空 deferred response 队列（释放 call_async callback ref，不调用 Lua 回调）
5. 清除 instance ref、移除 shared_ptr

---

## 回调签名汇总

**Server:**
```
service_handler(service_name, method_name, body_json) → response_json
```

**Client send:**
```
send_callback(msgid, service, method, body)
```

**Client async response:**
```
async_callback(body, err)
```

---

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `register_service` | service_name | `string` | `const char*` | `luaL_checkstring` |
| `register_service` | callback | `function` | `int` (registry ref) | `luaL_ref` |
| `call` / `call_async` | service, method | `string` | `const char*` | `luaL_checkstring` |
| `call` / `call_async` | args | `string` | `const char*` | `luaL_optstring` (default `"{}"`) |
| `call` | timeout_ms | `integer` | `lua_Integer` → `int` | `luaL_optinteger` (default 5000) |
| `call` | 返回值 body | `string` | `std::string` → `const char*` | `lua_pushstring` |
| `set_send_callback` | callback | `function` | `int` (registry ref) | `luaL_ref` |
| service cb 参数 | service_name, method_name | `string` | `const char*` + `size_t` | `lua_pushlstring` |
| service cb 参数 | body_json | `string` | `const char*` + `size_t` | `lua_pushlstring` |
| service cb 返回值 | response_json | `string` | `const char*` (via `lua_tostring`) | `lua_isstring` check |
| send cb 参数 | msgid | `integer` | `lua_Integer` | `lua_pushinteger` |

## Example

```lua
-- ── Server ──
local rpc_server = rpc.new_server()

rpc_server:register_service("game", function(svc, method, body)
    if method == "ping" then
        return '{"pong":true}'
    elseif method == "get_player" then
        return '{"name":"player1","level":10}'
    end
    return '{"error":"unknown method"}'
end)

-- ── Client ──
local rpc_client = rpc.new_client()

-- Set transport callback (e.g., forward to a TCP connection)
local conn = net.client.connect("127.0.0.1:9000")
rpc_client:set_send_callback(function(msgid, service, method, body)
    local wire = cmsgpack.pack({
        msgid = msgid,
        service = service,
        method = method,
        body = body,
    })
    conn:send(wire)
end)

-- Synchronous call
local body, err = rpc_client:call("game", "ping", "{}", 3000)
if body then
    log_info("RPC response: " .. body)
else
    log_error("RPC failed: " .. err)
end

-- Asynchronous call
rpc_client:call_async("game", "get_player", "{}", function(body, err)
    if body then
        log_info("Player data: " .. body)
    else
        log_error("Async RPC failed: " .. err)
    end
end)

-- Cleanup
rpc_client:stop()
rpc_server:stop()
```

## Lifecycle

- 服务端 pending 请求由 `UpdateRpcBindings()` 每帧排空，带 5ms 时间预算（超时后剩余请求保留在队列中，下帧继续处理）
- 客户端异步响应由 `UpdateRpcBindings()` 每帧排空
- 客户端超时由 `UpdateRpcBindings()` 每帧处理（`ProcessTimeouts()`）
- `ShutdownRpcBindings()` 在引擎关闭时释放所有 server/client 实例
- 实例通过 `__gc` metamethod 支持 Lua GC 自动回收（效果等价于 `stop()`）
- 共享所有权（`shared_ptr<RpcServerCtx/RpcClientCtx>`）确保 transport 线程回调中 `weak_ptr::lock()` 可安全检测实例是否已被销毁
