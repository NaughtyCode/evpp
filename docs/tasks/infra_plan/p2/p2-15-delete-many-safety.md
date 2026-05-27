# P2-15: kDeleteMany Empty Filter Safety — Require Explicit Confirmation

## Objective

Add a safety mechanism to prevent accidental full-collection deletion via `kDeleteMany` with an empty filter `{}`.

## Current State

`db_thread.cc:764-779` — `ProcessRequest` explicitly allows empty filters for `kDeleteMany`, with a SAFETY NOTE acknowledging the risk:

```cpp
// SAFETY NOTE: An empty filter "{}" matches ALL documents in the
// collection. The input validation above ensures bson_data is
// non-empty, but a caller could still pass "{}" as a valid JSON
// filter. This is accepted as intentional — the caller is
// responsible for providing a restrictive filter unless a
// full-collection delete is genuinely intended.
case DbOperation::kDeleteMany: {
    mongo::BsonDocument selector;
    if (!ParseJsonDoc(req.bson_data, "bson_data", &selector, &resp)) break;
    // ... proceeds with coll->DeleteMany(selector, ...)
}
```

A Lua-side typo (`db_send_request("delete_many", {collection = "players", filter = {}})` — forgot to fill in the filter) would delete every document in the collection. No confirmation, no safety latch beyond the comment.

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

**File**: `src/runtime/database/data_service/db_thread.cc`

```cpp
case DbOperation::kDeleteMany: {
    mongo::BsonDocument selector;
    if (!ParseJsonDoc(req.bson_data, "bson_data", &selector, &resp)) break;

    /* Safety check: empty filter requires explicit confirmation */
    if (selector.IsEmpty() && !req.allow_empty_filter) {
        resp.success = false;
        resp.error_message = "Empty filter rejected: set allow_empty_filter=true to confirm "
                             "intentional full-collection delete";
        EnqueueResponse(std::move(resp));
        return;
    }
    /* ... proceed with coll->DeleteMany(selector, ...) ... */
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
