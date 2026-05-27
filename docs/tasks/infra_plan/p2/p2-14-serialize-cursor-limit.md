# P2-14: SerializeCursor Document Count Limit — Prevent GB-level OOM

## Objective

Add a maximum document count limit to `SerializeCursor()` in `db_thread.cc` to prevent OOM when serializing large MongoDB query results into a JSON array string.

## Current State

`db_thread.cc:38-54` — `SerializeCursor()` iterates a MongoDB cursor and builds a JSON array string with **no document count limit**:

```cpp
std::string SerializeCursor(mongoc_cursor_t* cursor) {
    std::string json = "[";
    const bson_t* doc;
    bool first = true;
    while (mongoc_cursor_next(cursor, &doc)) {
        /* No limit — can iterate millions of documents */
        if (!first) json += ",";
        /* ... serialize doc to JSON string ... */
        json += bson_as_json_str(doc);
        first = false;
    }
    json += "]";
    return json;  /* Could be gigabytes */
}
```

A query returning 1 million documents with 1KB each would produce a 1GB string → OOM.

## Implementation Steps

### Step 1: Add Limit Parameter

**File**: `src/runtime/database/db_thread.cc`

```cpp
/* Maximum number of documents to serialize in a single cursor response */
static constexpr uint32_t kMaxCursorDocuments = 1000;

std::string SerializeCursor(mongoc_cursor_t* cursor, uint32_t max_documents = kMaxCursorDocuments) {
    std::string json = "[";
    const bson_t* doc;
    bool first = true;
    uint32_t count = 0;

    while (mongoc_cursor_next(cursor, &doc)) {
        if (count >= max_documents) {
            DB_LOG_WARN("SerializeCursor: reached limit of {} documents. "
                        "Results truncated.", max_documents);
            break;
        }

        if (!first) json += ",";
        char* doc_json = bson_as_json(doc, nullptr);
        if (doc_json) {
            json += doc_json;
            bson_free(doc_json);
        }
        first = false;
        count++;
    }

    json += "]";
    return json;
}
```

### Step 2: Return Truncation Indicator

Add a field to the response indicating truncation:

```cpp
struct CursorResult {
    std::string json;
    uint32_t returned_count = 0;
    bool truncated = false;  /* true if more documents exist */
};
```

### Step 3: Lua-Side Pagination

```lua
-- Lua API supports pagination
local result = cursor:next_batch(100)  -- get up to 100 documents
while result.truncated do
    for _, doc in ipairs(result.documents) do
        process(doc)
    end
    result = cursor:next_batch(100)
end
```

### Step 4: Tests

- Cursor with 0 documents: empty array, truncated=false
- Cursor with < limit documents: all returned, truncated=false
- Cursor with > limit documents: limited returned, truncated=true
- Subsequent `next_batch` calls return remaining documents
- No OOM with very large cursor (mock)

## Acceptance Criteria

1. `SerializeCursor` limits documents to configurable maximum (default 1000)
2. Response includes `truncated` flag when more documents exist
3. Lua API supports paginated cursor iteration
4. WARN log when truncation occurs
5. Tests verify limit enforcement and pagination

## Dependencies: None | Estimated Effort: ~50 lines
