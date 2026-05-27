# P2-15: kDeleteMany Empty Filter Safety — Require Explicit Confirmation

## Objective

Add a safety mechanism to prevent accidental full-collection deletion via `kDeleteMany` with an empty filter `{}`.

## Current State

`db_thread.cc:752-758` — `ProcessRequest` explicitly allows empty filters for `kDeleteMany`, marked as "intentional":

```cpp
case DbOperation::kDeleteMany:
    /* Empty filter is intentional — allows "delete all" */
    mongoc_collection_delete_many(collection, filter, nullptr, nullptr, &error);
```

A Lua-side typo (`db_send_request("delete_many", {collection = "players", filter = {}})` — forgot to fill in the filter) would delete every document in the collection. No confirmation, no safety latch.

## Impact

A single buggy Lua script can wipe an entire MongoDB collection. For production databases with millions of player records, this is catastrophic and potentially irreversible.

## Implementation Steps

### Step 1: Add Explicit Confirmation Flag

**File**: `src/runtime/database/db_types.h`

```cpp
struct DbRequest {
    /* ... existing fields ... */

    /*
     * When true, explicitly confirms that an empty filter {} for kDeleteMany
     * (and kDeleteOne) is intentional. Set to false by default.
     * Lua API: db_delete_many(collection, filter, {allow_empty_filter = true})
     */
    bool allow_empty_filter = false;
};
```

### Step 2: Check Before Delete

**File**: `src/runtime/database/db_thread.cc`

```cpp
case DbOperation::kDeleteMany: {
    /* Safety check: empty filter requires explicit confirmation */
    if (bson_empty(filter) && !request->allow_empty_filter) {
        DB_LOG_ERROR("kDeleteMany with empty filter rejected — "
                     "set allow_empty_filter=true to confirm intentional full delete. "
                     "Collection: '{}'", request->collection);
        /* Return error response */
        auto response = std::make_shared<DbResponse>();
        response->status = DbRequestStatus::kError;
        response->error_message = "Empty filter rejected: set allow_empty_filter=true to confirm";
        SendResponse(response);
        return;
    }
    /* ... proceed with delete ... */
}
```

### Step 3: Lua API

```lua
-- Safe (rejected):
db_send_request("delete_many", {collection = "players", filter = {}})

-- Explicit confirmation required:
db_send_request("delete_many", {
    collection = "players",
    filter = {},
    allow_empty_filter = true  -- I know what I'm doing
})
```

### Step 4: Tests

- Empty filter without confirmation → request rejected with error
- Empty filter with confirmation → delete proceeds
- Non-empty filter → delete proceeds normally (no confirmation needed)
- Error message is clear and descriptive

## Acceptance Criteria

1. `kDeleteMany` with empty filter rejects the request unless `allow_empty_filter = true`
2. Rejection produces a clear error message (not silent)
3. Lua API requires explicit `allow_empty_filter = true` for full-collection delete
4. Non-empty filters work normally without requiring the flag
5. Tests verify both rejection and confirmed execution

## Dependencies: None | Estimated Effort: ~80 lines
