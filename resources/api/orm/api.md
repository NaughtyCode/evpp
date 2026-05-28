# ORM System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 `orm.*` 函数均为同步调用，直接操作 `OrmSession` 单例。 |
| **线程安全** | 取决于 `OrmSession` 的实现。Lua binding 层本身不提供额外的线程同步——所有 `orm.*` 函数直接转发到 `OrmSession::Instance()`，无锁、无队列。若 ORM 内部使用了 mutex 保护，则线程安全；否则需在单线程上调用。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。 |

## Overview

The ORM (Object-Relational Mapping) system provides Lua bindings for defining collection schemas and performing CRUD operations. It exposes a simple document-oriented ORM backed by `OrmSession`, with schema registration and optional caching.

## Module

`orm` (global table)

## Functions

### `orm.define(collection_name, schema_table)`

Registers a collection schema with the ORM session. Defines fields with types and optional indexes.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection_name` | `string` | `const char*` (via `luaL_checkstring`) | Name of the collection / table |
| `schema_table` | `table` | Lua table (via `lua_getfield`) | Schema definition table |

**`schema_table` 结构：**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fields` | `table` | No | `{["field_name"] = "type_string", ...}`。支持的类型：`"int"`, `"double"`, `"bool"`, 其他字符串均视为 `"string"`。 |
| `indexes` | `table` | No | Array of index definitions: `{{fields = {"field1", "field2"}, unique = true}, ...}` |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `orm.find(collection, query_table)`

Finds documents matching a query. Returns an array of JSON document strings.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection` | `string` | `const char*` (via `luaL_checkstring`) | Collection name |
| `query_table` | `table` | Lua table (via `lua_next` 遍历) | Query filter as key-value table。仅 string → string 条目有效。 |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `results` | `table` | Lua table (array, via `lua_newtable` + `lua_rawseti`) | Array of JSON document strings. Empty array if no matches. |

### `orm.find_by_id(collection, id)`

Finds a single document by its ID.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection` | `string` | `const char*` (via `luaL_checkstring`) | Collection name |
| `id` | `string` | `const char*` (via `luaL_checkstring`) | Document ID |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `doc` | `string` or `nil` | `lua_pushstring` | JSON document string if found, `nil` otherwise |

### `orm.insert(collection, doc_json)`

Inserts a document into a collection.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection` | `string` | `const char*` (via `luaL_checkstring`) | Collection name |
| `doc_json` | `string` | `const char*` (via `luaL_checkstring`) | JSON-encoded document |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `orm.update(collection, id, update_json)`

Updates a document by ID.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection` | `string` | `const char*` (via `luaL_checkstring`) | Collection name |
| `id` | `string` | `const char*` (via `luaL_checkstring`) | Document ID to update |
| `update_json` | `string` | `const char*` (via `luaL_checkstring`) | JSON-encoded update descriptor |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `orm.delete(collection, id)`

Deletes a document by ID.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `collection` | `string` | `const char*` (via `luaL_checkstring`) | Collection name |
| `id` | `string` | `const char*` (via `luaL_checkstring`) | Document ID to delete |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `ok` | `boolean` | `int` (0/1 via `lua_pushboolean`) | `true` on success |

### `orm.cache_stats()`

Returns cache hit/miss statistics from the ORM session.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `stats` | `table` | Lua table (via `lua_newtable` + `lua_setfield`) | Statistics table with fields: `hits` (integer), `misses` (integer), `hit_rate` (number) |

**Returned table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `hits` | `integer` | Total cache hits |
| `misses` | `integer` | Total cache misses |
| `hit_rate` | `number` | Global hit rate (0.0 ~ 1.0) |

## Schema Field Types

| Lua Type String | C++ FieldType | Description |
|-----------------|---------------|-------------|
| `"int"` | `kInt` | 64-bit signed integer |
| `"double"` | `kDouble` | Double-precision float |
| `"bool"` | `kBool` | Boolean |
| 其他任意字符串 | `kString` | UTF-8 string (default) |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| 所有函数 | collection / id / doc_json / update_json | `string` | `const char*` | `luaL_checkstring` |
| `define` | schema_table | `table` | Lua table | `lua_getfield` + `lua_next` 遍历 |
| `find` | query_table | `table` | Lua table (string→string) | `lua_next` + `lua_isstring` |
| `find` / `find_by_id` | 返回值 | `table` (JSON string array) / `string` / `nil` | `std::string` / `std::optional<std::string>` → `const char*` | `lua_pushstring` / `lua_pushnil` |
| `cache_stats` | hits / misses | `integer` | `uint64_t` → `lua_Integer` | `lua_pushinteger` |
| `cache_stats` | hit_rate | `number` | `double` | `lua_pushnumber` |

## Example

```lua
-- Define a schema
orm.define("players", {
    fields = {
        name = "string",
        score = "int",
        level = "int",
        active = "bool",
        win_rate = "double",
    },
    indexes = {
        { fields = {"score"}, unique = false },
        { fields = {"name"}, unique = true },
    },
})

-- Insert documents
orm.insert("players", '{"name":"player1","score":500,"level":10,"active":true,"win_rate":0.75}')
orm.insert("players", '{"name":"player2","score":300,"level":5,"active":false,"win_rate":0.42}')

-- Find by query
local results = orm.find("players", { active = "true" })
for i, doc in ipairs(results) do
    log_info("Player " .. i .. ": " .. doc)
end

-- Find by ID
local player = orm.find_by_id("players", "player1")
if player then
    log_info("Found: " .. player)
end

-- Update
orm.update("players", "player1", '{"$set":{"score":600,"level":11}}')

-- Delete
orm.delete("players", "player2")

-- Check cache performance
local stats = orm.cache_stats()
log_info(string.format("Cache: hits=%d misses=%d hit_rate=%.2f",
    stats.hits, stats.misses, stats.hit_rate))
```
