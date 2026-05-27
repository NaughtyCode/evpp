# HTTP Client API (`net.http`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`get()` / `post()` 发起异步 HTTP 请求，请求在 evpp HTTP client 内部线程上执行，响应通过 EventLoop 回调。 |
| **线程安全** | 否。必须在 ScriptVM 所属的主线程上调用。`g_net_alive`（`std::atomic<bool>`）和 `g_http_pending_refs`（`std::vector<int>` + `std::mutex`）用于保护回调在 shutdown 期间的安全释放，但不支持多线程并发调用 API。 |
| **回调线程** | 主线程（EventLoop 线程）。`HandleHttpResponse` 在 `resp->Execute` 的回调中执行，通过 EventLoop 触发。 |

## Overview

The HTTP client sub-module provides asynchronous HTTP GET and POST requests. Callbacks are invoked on the main event loop thread when responses arrive.

## Module Path

`net.http`

## Functions

### `net.http.get(url, callback)`

Sends an asynchronous HTTP GET request.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `url` | `string` | `const char*` (via `luaL_checkstring`) | Full URL to request |
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Called when the response arrives. Signature: `function(status_code, body)` |

| Returns | — | 无返回值 |

The timeout is configured via `ServerConfig.http.timeout_sec`.

### `net.http.post(url, body, callback)`

Sends an asynchronous HTTP POST request.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `url` | `string` | `const char*` (via `luaL_checkstring`) | Full URL to request |
| `body` | `string` | `const char*` + `size_t` (via `luaL_checklstring` → `std::string`) | Request body data |
| `callback` | `function` | `int` (Lua registry ref via `luaL_ref`) | Called when the response arrives. Signature: `function(status_code, body)` |

| Returns | — | 无返回值 |

## Callback Signature

```
callback(status_code, body)
```
- `status_code`: `integer` — HTTP status code（0 if request failed）。C 类型：`lua_Integer`（`lua_pushinteger`）。
- `body`: `string` — Response body（empty string if request failed）。C 类型：`const char*` + `size_t`（`lua_pushlstring`）。

## 回调生命周期

- 回调 ref 在发起请求时通过 `luaL_ref` 存入 registry，同时加入 `g_http_pending_refs` 向量（受 `g_http_mutex` 保护）
- `HandleHttpResponse` 在分派前先从 pending 列表中摘除该 ref，然后调用 `lua_pcall`
- shutdown 时 `ShutdownHttpBindings` 设置 `g_net_alive = false`，批量释放所有 pending ref
- 回调中调用 `net.http.get/post` 不会死锁（dispatch Lua 时未持有 mutex）

## Configuration

HTTP timeout is read from `ServerConfig.http.timeout_sec` at request time. The default is defined in `config_constants.h` as `kDefaultHttpTimeoutSec`.

## Example

```lua
-- GET request
net.http.get("http://api.example.com/data", function(code, body)
    if code == 200 then
        log_info("Got response: " .. body)
    else
        log_error("HTTP GET failed: " .. code)
    end
end)

-- POST request
local payload = cmsgpack.pack({action = "login", user = "admin"})
net.http.post("http://api.example.com/auth", payload, function(code, body)
    if code == 200 then
        log_info("Auth success: " .. body)
    else
        log_error("Auth failed with code " .. code)
    end
end)
```

## Lifecycle

- In-flight callbacks are guarded by an atomic `g_net_alive` flag
- During shutdown (`ShutdownHttpBindings`), the flag is set to false and all pending callback refs are released
- Callbacks that arrive after shutdown begin but before the Lua state is destroyed are safely dropped
- The Lua state is guaranteed valid during `ShutdownHttpBindings` because `DestroyScript` runs after `ShutdownNetBindings`
