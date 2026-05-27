# P3-1: Multi-Database Backend Abstraction

## Objective

Abstract the database layer behind a common interface, enabling support for multiple backends (MongoDB, PostgreSQL, Redis, SQLite) without changing Lua business logic.

## Current State

Only MongoDB is supported. All database code assumes MongoDB-specific types (`bson_t`, `mongoc_collection_t`). No abstract interface — switching to another database would require rewriting all database-using Lua scripts.

## Implementation Steps

### Step 1: Define Abstract Database Interface

**File**: `src/runtime/database/db_backend.h`

```cpp
class IDatabaseBackend {
public:
    virtual ~IDatabaseBackend() = default;
    virtual bool Initialize(const DbConfig& config) = 0;
    virtual void Shutdown() = 0;

    virtual DbResult Find(const std::string& collection, const Query& query, const FindOptions& opts) = 0;
    virtual DbResult Insert(const std::string& collection, const Document& doc) = 0;
    virtual DbResult Update(const std::string& collection, const Query& query, const Document& update) = 0;
    virtual DbResult Delete(const std::string& collection, const Query& query, bool delete_one) = 0;
    virtual DbResult Count(const std::string& collection, const Query& query) = 0;
    virtual DbResult Aggregate(const std::string& collection, const Pipeline& pipeline) = 0;

    virtual std::unique_ptr<IDatabaseCursor> FindCursor(const std::string& collection, const Query& query) = 0;
    virtual bool IsHealthy() = 0;
};
```

### Step 2: Implement MongoDB Backend

Refactor existing MongoDB code behind the interface.

### Step 3: Implement SQLite Backend (for dev/testing)

```cpp
class SQLiteBackend : public IDatabaseBackend {
    /* Zero-configuration backend for local development and testing.
     * No external database server required. */
};
```

### Step 4: Add Config

```json
{
  "database": {
    "backend": "mongodb",
    "backends": {
      "mongodb": { "uri": "mongodb://localhost:27017", "db": "game" },
      "sqlite": { "path": "artifacts/game.db" }
    }
  }
}
```

### Step 5: Tests

- Interface compliance test: same test suite runs against all backends
- SQLite backend passes all CRUD tests
- Backend switching at startup based on config

## Acceptance Criteria

1. `IDatabaseBackend` interface with CRUD + cursor operations
2. MongoDB backend implements the interface (refactored from current code)
3. SQLite backend for local development
4. Backend selectable via config
5. Lua API unchanged regardless of backend

## Dependencies: None | Estimated Effort: ~500 lines
