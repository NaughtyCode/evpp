# P3-9: MongoDB Binding Return Value Unification

## Objective

Unify the inconsistent return value patterns across MongoDB binding functions (some return 2 values, some return 3).

## Current State

MongoDB binding functions have inconsistent return value counts:

```lua
-- 2-value returns (bool, err|nil):
local ok, err = collection:insert_one(doc)
local ok, err = collection:delete_one(filter)

-- 3-value returns (bool, err|nil, doc|nil):
local ok, err, doc = collection:find_one(filter)
local ok, err, result = collection:update_one(filter, update)
```

Lua developers must memorize which functions return 2 vs 3 values. Getting it wrong means `nil` in unexpected places.

## Implementation Steps

### Step 1: Unify on 3-Value Returns

All MongoDB binding functions return `(bool, err_or_nil, result_or_nil)`:

```lua
-- All operations return 3 values consistently:
local ok, err, result = collection:insert_one(doc)
local ok, err, result = collection:find_one(filter)
local ok, err, result = collection:delete_one(filter)
local ok, err, result = collection:update_one(filter, update)

-- result is nil for operations without return data (insert, delete)
-- result is a document for operations that return data (find)
```

### Step 2: Update All Binding Functions

**Files**: `src/runtime/database/mongo_bind/bind_collection.cc`, `bind_cursor.cc`, `bind_database.cc`, etc.

For each function:
1. Ensure `lua_pushboolean(L, success)` is first return value
2. Ensure `lua_pushstring(L, error_message)` or `lua_pushnil(L)` is second
3. Ensure result data or `lua_pushnil(L)` is third
4. Return exactly 3 values

### Step 3: Update Lua Scripts

Update all Lua scripts that use the 2-value pattern to handle 3 values. Add backward compatibility wrapper:

```lua
-- resources/script/db_compat.lua (temporary, remove after migration)
local mongo = {}
for name, fn in pairs(raw_mongo) do
    mongo[name] = function(...)
        local ok, err, result = fn(...)
        return ok, err, result  -- always 3 values
    end
end
```

### Step 4: Tests

- Verify all binding functions return exactly 3 values
- Verify 3rd value is nil for operations without result data
- Verify existing Lua tests pass with new return pattern

## Acceptance Criteria

1. All MongoDB binding functions return exactly 3 values: (bool, err|nil, result|nil)
2. Operations without result data push nil as 3rd value
3. All existing tests updated and pass
4. Lua API documentation updated

## Dependencies: None | Estimated Effort: ~200 lines
