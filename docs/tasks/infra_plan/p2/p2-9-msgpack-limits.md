# P2-9: msgpack Encode Size/Depth Limits

## Objective

Add maximum payload size and nesting depth checks to msgpack encoding to prevent memory exhaustion and stack overflow from malicious or buggy Lua scripts.

## Current State

`l_msgpack_pack` can encode Lua tables of arbitrary depth and arbitrary size. No limits exist:
- A deeply nested table (depth > 1000) can overflow the C stack during recursive encoding
- A very large table (millions of entries) can produce gigabytes of encoded output

## Implementation Steps

### Step 1: Add Depth and Size Parameters

**File**: `src/runtime/script/msgpack_bind.cc`

```cpp
/* Default limits */
static constexpr int kDefaultMaxDepth = 64;
static constexpr size_t kDefaultMaxPayloadSize = 1024 * 1024;  /* 1 MB */

struct EncodeContext {
    int max_depth = kDefaultMaxDepth;
    size_t max_payload_size = kDefaultMaxPayloadSize;
    int current_depth = 0;
    size_t current_size = 0;
    lua_State* L = nullptr;
};

/* Recursive encode with depth tracking */
static bool EncodeValue(EncodeContext& ctx, msgpack_packer* pk, int index) {
    if (ctx.current_size > ctx.max_payload_size) {
        lua_pushstring(ctx.L, "msgpack encode: payload exceeds maximum size");
        return false;
    }

    if (ctx.current_depth > ctx.max_depth) {
        lua_pushstring(ctx.L, "msgpack encode: nesting depth exceeds maximum");
        return false;
    }

    ctx.current_depth++;
    /* ... encode logic ... */
    ctx.current_depth--;

    return true;
}
```

### Step 2: Configurable Limits

```json
{
  "msgpack": {
    "limits": {
      "max_payload_size": 1048576,
      "max_nesting_depth": 64
    }
  }
}
```

### Step 3: Lua API

```lua
-- Explicit limits per call:
local data = cmsgpack.pack(large_table, {max_depth = 32, max_size = 512 * 1024})

-- Safe mode (default limits):
local data = cmsgpack_safe.pack(large_table)  -- errors on exceed
```

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/msgpack_limits_test.cc`:
- Encode table with depth = 10 → succeeds (under default limit of 64)
- Encode table with depth = 65 → fails with clear error "nesting depth exceeds maximum"
- Encode table with depth = 1000 → fails without stack overflow (caught by limit check)
- Encode table producing payload = 512KB → succeeds (under 1MB default)
- Encode table producing payload = 2MB → fails with "payload exceeds maximum size"
- Custom limits via config: `max_depth = 32, max_size = 64KB` → enforced
- `cmsgpack_safe.pack()` applies default limits automatically
- Zero-size payload (empty table) → succeeds
- Single-element table → succeeds with correct output

**Lua tests** — `src/tests/lua/msgpack_safe_test.lua`:
- Test `cmsgpack_safe.pack()` with nested table at limit boundary
- Test explicit per-call limits override config defaults
- Test error message format is parseable by Lua error handler

```
src/tests/unit/msgpack_limits_test.cc    # ~80 lines
src/tests/lua/msgpack_safe_test.lua      # ~50 lines
```

## Acceptance Criteria

1. msgpack encode fails with clear error when depth exceeds limit
2. msgpack encode fails with clear error when payload size exceeds limit
3. Limits are configurable via JSON config
4. `cmsgpack_safe` applies default limits automatically
5. Tests verify depth and size limits at boundaries
6. Tests verify configurable limits override defaults
7. Tests verify no stack overflow on depth = 1000

## Dependencies: P0-3 (Test Infrastructure) | Estimated Effort: ~80 lines + ~130 lines tests
