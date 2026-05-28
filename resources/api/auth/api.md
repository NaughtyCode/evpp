# Auth System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 `auth.*` 函数均为同步调用，直接操作 `SessionManager` 单例。 |
| **线程安全** | 取决于 `SessionManager` 的实现。Lua binding 层本身不提供额外的线程同步——所有 `auth.*` 函数直接转发到 `SessionManager::Instance()`，无锁、无队列。若 `SessionManager` 内部使用了 mutex 保护，则线程安全；否则需在单线程上调用。**注意：`SessionManager` 是进程级全局单例，所有 ScriptVM 共享同一会话状态，不可按 VM 隔离。** |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。 |

## Overview

The Auth system provides Lua bindings for session management and token-based authentication. It exposes functions for creating/validating/revoking sessions and adding token-based credentials.

## Module

`auth` (global table)

## Functions

### `auth.set_token_backend()`

Activates the token-based authentication backend. Creates a new `TokenAuthBackend` and sets it on the `SessionManager`. Subsequent calls to `auth.add_token()` and `auth.authenticate()` will use this backend.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `auth.add_token(token, entity_id)`

Adds a token-to-entity mapping to the token authentication backend. If no backend has been set via `set_token_backend()`, one is created automatically on first call.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `token` | `string` | `const char*` (via `luaL_checkstring`) | Authentication token |
| `entity_id` | `string` | `const char*` (via `luaL_checkstring`) | Entity ID associated with this token |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `auth.authenticate(method, params_table)`

Authenticates using the specified method and parameters.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `method` | `string` | `const char*` (via `luaL_checkstring`) | Authentication method name |
| `params_table` | `table` | Lua table (via `lua_next` 遍历) | Key-value parameters. Only string→string entries are extracted. |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |
| `session_id` | `string` | `lua_pushstring` | Session ID (non-empty on success) |
| `err` | `string` | `lua_pushstring` | Error message on failure |

返回值数量：成功返回 2 个值（true, session_id），失败返回 2 个值（false, "authentication failed"）。

### `auth.create_session(entity_id)`

Creates a new session for the given entity.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `entity_id` | `string` | `const char*` (via `luaL_checkstring`) | Entity identifier for the session |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `session_id` | `string` or `nil` | `lua_pushstring` / `lua_pushnil` | Session ID string on success, `nil` if creation failed。返回 1 个值。 |

### `auth.validate_session(session_id)`

Checks whether a session is still valid.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `session_id` | `string` | `const char*` (via `luaL_checkstring`) | Session ID to validate |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `valid` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` if the session is valid |

### `auth.revoke_session(session_id)`

Revokes (invalidates) a session.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `session_id` | `string` | `const char*` (via `luaL_checkstring`) | Session ID to revoke |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `auth.cleanup_expired()`

Removes all expired sessions from the session manager.

| Returns | — | 无返回值 |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| 所有函数 | token / entity_id / method / session_id | `string` | `const char*` | `luaL_checkstring` |
| `authenticate` | params_table | `table` | Lua table (string→string) | `lua_next` + `lua_isstring` |
| `authenticate` / `create_session` | 返回值 session_id | `string` | `std::string` → `const char*` | `lua_pushstring` |
| 所有函数 | 返回值 ok / valid | `boolean` | `int` (0/1) | `lua_pushboolean` |

## Example

```lua
-- Setup token-based authentication
auth.set_token_backend()
auth.add_token("secret-token-123", "player_001")
auth.add_token("admin-token-456", "admin_001")

-- Authenticate with token
local params = { token = "secret-token-123" }
local ok, session_id = auth.authenticate("token", params)
if ok then
    log_info("Authenticated: session = " .. session_id)
else
    log_error("Authentication failed: " .. session_id)  -- session_id is error msg
end

-- Create session directly for an entity
local sid = auth.create_session("player_002")
if sid then
    log_info("Session created: " .. sid)
end

-- Validate a session
if auth.validate_session(sid) then
    log_info("Session is valid")
else
    log_info("Session expired or invalid")
end

-- Revoke a session
auth.revoke_session(sid)

-- Cleanup stale sessions periodically
auth.cleanup_expired()
```
