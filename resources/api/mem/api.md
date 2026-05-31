# Memory Stats API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程。所有 `mem.*` 函数同步读取或重置 `MemStats` 单例。 |
| **线程安全** | 取决于 `MemStats` 内部实现；Lua binding 不提供额外同步。 |
| **回调线程** | 无回调。 |

## Overview

The memory stats module exposes runtime allocation statistics when the engine is built with `ENGINE_MEM_STATS_ENABLED`. If that compile flag is not enabled, the `mem` module is not exported.

## Module

`mem` (global table, conditional)

## Functions

### `mem.is_enabled()`

Returns `true`. This function exists only when the module is compiled in.

| Returns | Type | Description |
|---------|------|-------------|
| `enabled` | `boolean` | Always `true` when `mem` is available |

### `mem.get_stats()`

Returns a snapshot table.

| Returns | Type | Description |
|---------|------|-------------|
| `stats` | `table` | Current memory statistics |

Returned table shape:

| Field | Type | Description |
|-------|------|-------------|
| `totals` | `table` | Aggregate allocation/free counters |
| `by_operation` | `table` | Per-operation counters keyed by operation name |
| `size_buckets` | `table` | Allocation counts/bytes by size bucket |
| `recent_allocs` | `table` | Array of recent allocation records |
| `active_alloc_count` | `integer` | Currently active allocation count |

`totals` fields:

| Field | Type |
|-------|------|
| `alloc_count` | `integer` |
| `free_count` | `integer` |
| `alloc_bytes` | `integer` |
| `free_bytes` | `integer` |
| `bytes_in_use` | `integer` |
| `peak_bytes` | `integer` |
| `peak_alloc_count` | `integer` |

Operation counters contain `{ alloc_count, free_count, alloc_bytes, free_bytes }`.

Size buckets are keyed by `lt_64`, `64_256`, `256_1k`, `1k_4k`, `4k_64k`, and `gt_64k`; each bucket contains `{ count, bytes }`.

Recent allocation records contain `{ file, line, size, op }`.

### `mem.reset_stats()`

Resets all tracked memory statistics.

| Returns | — | No return value |

### `mem.dump_stats(filepath)`

Dumps memory statistics to a file.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `filepath` | `string` | `const char*` (via `luaL_checkstring`) | Output path |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `boolean` | `true` if the dump succeeded |

## Example

```lua
if mem and mem.is_enabled() then
    local stats = mem.get_stats()
    log_info("bytes in use: " .. stats.totals.bytes_in_use)
    mem.dump_stats("logs/mem_stats.json")
end
```
