# P3-10: Cursor Pre-allocation Pattern Safety

## Objective

Fix the unsafe pre-allocation pattern in `l_cursor_next` (`mongo_bind/bind_cursor.cc:40`) where a `BsonDocument` is allocated before calling `cursor->Next()` — requiring manual cleanup on failure.

## Current State

`mongo_bind/bind_cursor.cc:40` — `l_cursor_next` pre-allocates a `BsonDocument` and then calls `cursor->Next()`:

```cpp
int l_cursor_next(lua_State* L) {
    auto* cursor = GetCursor(L, 1);
    auto* doc = new BsonDocument();  /* pre-allocate — must clean up on error */

    if (!cursor->Next(doc)) {
        delete doc;                  /* manual cleanup — easy to forget */
        lua_pushnil(L);
        return 1;
    }

    /* ... use doc ... */
    delete doc;
    return 1;
}
```

This pattern is repeated in `mongo_bind/bind_collection.cc` multiple times. If an exception occurs between `new` and `delete`, or if a future maintainer adds an early return, `doc` leaks.

## Implementation Steps

### Step 1: Use unique_ptr for Automatic Cleanup

**File**: `src/runtime/database/mongo_bind/bind_cursor.cc`

```cpp
int l_cursor_next(lua_State* L) {
    auto* cursor = GetCursor(L, 1);
    auto doc = std::make_unique<BsonDocument>();  /* RAII-protected */

    if (!cursor->Next(doc.get())) {
        /* doc automatically freed — no manual delete needed */
        lua_pushnil(L);
        return 1;
    }

    ConvertBsonToLua(L, doc.get());
    /* doc automatically freed on return */
    return 1;
}
```

### Step 2: Apply to All Cursor and Collection Bindings

Search for all `new BsonDocument()` / `new BsonValue()` in the mongo_bind directory. Convert each to `std::make_unique`.

### Step 3: Tests

- Normal cursor iteration: no leak (verified with ASAN)
- cursor->Next() failure: doc freed (verified with ASAN)
- Exception thrown mid-function: doc freed (verified with ASAN)

## Acceptance Criteria

1. All `new BsonDocument()` in binding code replaced with `std::make_unique<BsonDocument>()`
2. Zero manual `delete` calls for BsonDocument in binding code
3. ASAN clean during cursor operations
4. Existing cursor tests pass

## Dependencies: None | Estimated Effort: ~80 lines
