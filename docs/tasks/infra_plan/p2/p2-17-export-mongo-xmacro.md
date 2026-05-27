# P2-17: ExportMongo X-macro Simplification — 246 to 82 Maintenance Points

## Objective

Reduce the ExportMongo maintenance burden from 246 manual registration points (82 types × 3 locations) to 82 entries in a single X-macro list.

## Current State

`mongo_bind.cc` — `ExportMongo()` function (254 lines) contains:

```cpp
/* 82 metatable registrations: */
RegisterClientMeta(L);
RegisterCollectionMeta(L);
RegisterCursorMeta(L);
/* ... 79 more ... */

/* 74 AddToModule calls: */
AddToModule(L, "mongoc", "client", l_mongo_client_create);
AddToModule(L, "mongoc", "collection", l_mongo_collection_find);
/* ... 72 more ... */
```

Plus corresponding `#include` headers. Each MongoDB type appears in 3 locations: RegisterMeta, AddToModule, and #include. Total: 82 × 3 = 246 maintenance points.

## Root Cause

Metatable registration and module export are separate steps joined only by naming convention. No macro or template automates "define type → register metatable → add functions to module."

## Implementation Steps

### Step 1: Define X-macro Type List

**File**: `src/runtime/database/mongo_types.def`

```cpp
/* X-macro list of all MongoDB types.
 * Format: X(lua_name, CppName, func1, func2, ...)
 * This is the SINGLE source of truth for MongoDB type registration.
 */
#ifndef MONGOC_TYPE
#error "MONGOC_TYPE must be defined before including this file"
#endif

MONGOC_TYPE(client, Client,
    (create, l_mongo_client_create)
    (destroy, l_mongo_client_destroy)
    (get_collection, l_mongo_client_get_collection)
    (get_database, l_mongo_client_get_database)
)

MONGOC_TYPE(collection, Collection,
    (find, l_mongo_collection_find)
    (find_one, l_mongo_collection_find_one)
    (insert_one, l_mongo_collection_insert_one)
    (insert_many, l_mongo_collection_insert_many)
    (update_one, l_mongo_collection_update_one)
    (update_many, l_mongo_collection_update_many)
    (delete_one, l_mongo_collection_delete_one)
    (delete_many, l_mongo_collection_delete_many)
    (count, l_mongo_collection_count)
    (aggregate, l_mongo_collection_aggregate)
)

MONGOC_TYPE(cursor, Cursor,
    (next, l_mongo_cursor_next)
    (next_batch, l_mongo_cursor_next_batch)
    (close, l_mongo_cursor_close)
)

/* ... 79 more types ... */

#undef MONGOC_TYPE
```

### Step 2: Generate RegisterMeta from X-macro

**File**: `src/runtime/database/mongo_bind.cc`

```cpp
/* Auto-generate all metatable registrations: */
#define MONGOC_TYPE(lua_name, CppName, ...) \
    Register##CppName##Meta(L);
#include "mongo_types.def"

/* Auto-generate all AddToModule calls: */
#define MONGOC_TYPE(lua_name, CppName, ...)                        \
    do {                                                            \
        AddToModule(L, "mongoc", #lua_name, __VA_ARGS__);          \
    } while(0)
#include "mongo_types.def"
```

### Step 3: Auto-Generate Includes

```cpp
/* Each type's header is included based on the CppName: */
#define MONGOC_INCLUDE(CppName) #include "mongo_bind/bind_##CppName##.h"
```

### Step 4: Backward Compatibility

Keep the old `ExportMongo()` working during transition. The X-macro version can be enabled via a compile-time flag.

### Step 5: Tests

- Verify all 82 types are registered (compare old and new ExportMongo output)
- Add a new type using only the X-macro list — verify it appears in Lua
- Remove a type — verify it's removed from all 3 registration points

## Acceptance Criteria

1. Single X-macro list (`mongo_types.def`) defines all MongoDB types and their functions
2. `ExportMongo()` is automatically generated from the X-macro list
3. Adding a new type requires modifying only the X-macro list
4. 246 manual registration points reduced to 82 X-macro entries
5. All existing MongoDB tests pass with X-macro-generated registration
6. Build time is not significantly affected

## Dependencies: None | Estimated Effort: ~200 lines
