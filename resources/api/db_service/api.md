# Database Service API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`db_send_request()` / `db_poll_response()` / `db_is_running()` / `db_is_healthy()` / `db_get_thread_count()` 必须在 ScriptVM 所属的主线程上调用。数据库 I/O 在独立的 DB 工作线程上执行。 |
| **线程安全** | 是（无锁 SPSC 队列架构）。请求通过 lock-free SPSC 队列发送到 DB 工作线程，响应通过另一端的 SPSC 队列返回。Round-robin 请求分发使用原子计数器（`std::atomic<uint64_t>`）。但 Lua 函数入口本身不可跨线程并发调用。 |
| **回调线程** | 无回调。采用主动轮询模式——每帧调用 `db_poll_response()` 从响应队列获取已完成的结果，所有响应在调用线程上同步返回。 |

## Overview

The Database Service provides asynchronous, non-blocking database operations from Lua scripts. It uses a multi-threaded architecture with lock-free SPSC queues for communication between the main thread and database worker threads.

## Module

Global functions (prefixed with `db_`).

## Functions

### `db_is_running()`

Checks if the DatabaseService is initialized and running.

| Returns | Type | Description |
|---------|------|-------------|
| `running` | `boolean` | `true` if the service is running |

### `db_is_healthy()`

Checks if all database worker threads are healthy.

| Returns | Type | Description |
|---------|------|-------------|
| `healthy` | `boolean` | `true` if all DBThreads report healthy |

### `db_get_thread_count()`

Returns the number of database worker threads.

| Returns | Type | Description |
|---------|------|-------------|
| `count` | `integer` | Number of DBThread workers (0 if not initialized) |

### `db_next_request_id()`

Returns a monotonically increasing request id that can be used as `req.request_id`.

| Returns | Type | Description |
|---------|------|-------------|
| `request_id` | `integer` | Next request id |

### `db_metrics()`

Returns current DatabaseService counters.

| Returns | Type | Description |
|---------|------|-------------|
| `metrics` | `table` | `{enqueued, dropped, completed, errors}` |

### `db_send_request(req)`

Sends an asynchronous database operation request. The request is routed to a DBThread via round-robin.

| Parameter | Type | Description |
|-----------|------|-------------|
| `req` | `table` | Request descriptor table (see Request Table Fields) |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `boolean` | `true` if the request was enqueued |
| `request_id_or_error` | `integer` or `string` | Request id on success, error message on failure |

Returns `false` + error message if the service is not running, the operation is unknown, or the queue is full.

### `db_poll_response()`

Polls for completed database operation responses (non-blocking).

| Returns | Type | Description |
|---------|------|-------------|
| `response` | `table` or `nil` | Response table, or `nil` if no response available |

## Request Table Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `request_id` | `integer` | No | Echoed in response; auto-assigned when 0 or absent |
| `operation` | `string` or `integer` | **Yes** | Operation name or enum value |
| `database` | `string` | CRUD only | Target database name |
| `collection` | `string` | CRUD only | Target collection name |
| `bson_data` | `string` | Varies | JSON filter / insert document / command / pipeline |
| `bson_data2` | `string` | Varies | JSON update descriptor |
| `script` | `string` | kExecuteScript | Lua script to execute on DB thread |
| `limit` | `integer` | No | Max documents (kFind), 0 = unlimited |
| `skip` | `integer` | No | Documents to skip (kFind) |
| `max_result_documents` | `integer` | No | Safety cap for serialized cursor results, default 1000, 0 = no extra cap |
| `allow_empty_filter` | `boolean` | No | Required for empty update/delete filters |

## Operation Names

| String | Description |
|--------|-------------|
| `"find"` | Find multiple documents |
| `"find_one"` | Find a single document |
| `"insert_one"` | Insert one document |
| `"insert_many"` | Insert multiple documents |
| `"update_one"` | Update one document |
| `"update_many"` | Update multiple documents |
| `"delete_one"` | Delete one document |
| `"delete_many"` | Delete multiple documents |
| `"count"` | Count documents |
| `"aggregate"` | Run aggregation pipeline |
| `"command"` | Run a database command |
| `"execute_script"` | Execute Lua script on DB thread |
| `"noop"` | Internal sentinel; rejected by `db_send_request` |

Operation names are case-insensitive.

## Response Table Fields

| Field | Type | Description |
|-------|------|-------------|
| `request_id` | `integer` | Echoed from the request |
| `status` | `integer` | Request lifecycle status enum |
| `success` | `boolean` | Whether the operation succeeded |
| `error_code` | `integer` | MongoDB wire-protocol error code |
| `error_message` | `string` | Human-readable error description |
| `result_data` | `string` | JSON result data (format varies by operation) |
| `affected_count` | `integer` | Documents matched/modified/deleted |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `db_send_request` | req (table) | `table` | Lua table (via `lua_getfield` for each field) | 从 table 中按字段名提取值 |
| `db_send_request` | operation (in req) | `string` or `integer` | `const char*` → `DbOperation` enum or `lua_Integer` | `lua_tostring` → case-insensitive match / `lua_tointeger` |
| `db_send_request` | request_id (in req) | `integer` | `uint64_t` | `lua_tointeger` (default 0 if absent) |
| `db_send_request` | database / collection (in req) | `string` | `std::string` | `lua_tostring` |
| `db_send_request` | bson_data / bson_data2 / script (in req) | `string` | `std::string` | `lua_tostring` |
| `db_send_request` | limit / skip (in req) | `integer` | `int` | `lua_tointeger` |
| `db_send_request` | 返回值 | `boolean` | `bool` → `int` (0/1) | `lua_pushboolean` |
| `db_poll_response` | 返回值 (response) | `table` or `nil` | Lua table (新构建) | `lua_newtable` + `lua_setfield` for each field |
| `db_poll_response` | request_id (in resp) | `integer` | `uint64_t` → `lua_Integer` | `lua_pushinteger` |
| `db_poll_response` | success (in resp) | `boolean` | `bool` → `int` (0/1) | `lua_pushboolean` |
| `db_poll_response` | error_code (in resp) | `integer` | `uint32_t` → `lua_Integer` | `lua_pushinteger` |
| `db_poll_response` | error_message (in resp) | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |
| `db_poll_response` | result_data (in resp) | `string` | `std::string` → `const char*` + `size_t` | `lua_pushlstring` |
| `db_poll_response` | affected_count (in resp) | `integer` | `int32_t` → `lua_Integer` | `lua_pushinteger` |
| `db_is_running` / `db_is_healthy` | 返回值 | `boolean` | `bool` → `int` (0/1) | `lua_pushboolean` |
| `db_get_thread_count` | 返回值 | `integer` | `size_t` → `lua_Integer` | `lua_pushinteger` |

## Example

```lua
-- Check health before sending
if not db_is_healthy() then
    log_error("DB service is unhealthy")
    return
end

-- Send a find request
local ok, request_id = db_send_request({
    request_id = 1001,
    operation = "find",
    database = "game_db",
    collection = "players",
    bson_data = '{"score": {"$gt": 100}}',
    limit = 10,
    max_result_documents = 1000,
})
if not ok then
    log_warn("db_send_request failed: " .. tostring(request_id))
end

-- Send an insert
db_send_request({
    request_id = 1002,
    operation = "insert_one",
    database = "game_db",
    collection = "players",
    bson_data = '{"name": "player1", "score": 500}',
})

-- Poll for responses (call each frame)
local resp = db_poll_response()
if resp then
    if resp.success then
        log_info("Request " .. resp.request_id .. " succeeded: " .. resp.result_data)
    else
        log_error("DB error [" .. resp.error_code .. "]: " .. resp.error_message)
    end
end
```

## Architecture

- Main thread communicates with DB worker threads via lock-free SPSC queues
- Round-robin request distribution via atomic counter
- Each DBThread borrows a MongoClient from a shared pool, processes the request, and returns the client
- Thread count and pool size are configured via `DbServiceConfig`
