# P2-2: ORM + Cache Layer for Database

## Objective

Add an object-relational mapping (ORM) layer and an in-memory cache in front of MongoDB to reduce per-request latency and database load.

## Current State

All database requests go directly: Lua → DatabaseService → SPSC queue → DBThread → MongoDB network IO → back. No abstraction, no caching. Developers manually construct BSON documents.

## Implementation Steps

### Step 1: Define ORM Interface

**File**: `src/runtime/database/orm.h`

```cpp
/* Define a database schema — maps C++/Lua types to MongoDB collections */
struct CollectionSchema {
    std::string collection_name;
    std::vector<FieldDef> fields;
    std::vector<IndexDef> indexes;
};

/* ORM session — provides typed CRUD operations */
class OrmSession {
public:
    template<typename T>
    std::optional<T> FindById(const std::string& id);

    template<typename T>
    std::vector<T> Find(const Query& query, const FindOptions& options = {});

    template<typename T>
    bool Insert(T& entity);

    template<typename T>
    bool Update(const T& entity);

    template<typename T>
    bool DeleteById(const std::string& id);
};
```

### Step 2: Implement Cache Layer

**File**: `src/runtime/database/cache.h`

```cpp
/* LRU cache for database entities */
template<typename T>
class EntityCache {
public:
    explicit EntityCache(size_t max_entries = 10000);

    std::optional<T> Get(const std::string& id);
    void Put(const std::string& id, const T& entity);
    void Invalidate(const std::string& id);
    void Clear();

    /* Statistics */
    size_t HitCount() const;
    size_t MissCount() const;
    double HitRate() const;

private:
    size_t max_entries_;
    std::unordered_map<std::string, typename std::list<...>::iterator> map_;
    std::list<std::pair<std::string, T>> lru_list_;
};
```

### Step 3: Write-Through Cache Strategy

Cache invalidation on write:
- Insert → write to DB, add to cache
- Update → write to DB, update cache
- Delete → delete from DB, remove from cache

### Step 4: Lua Binding

```lua
-- Define schema
local PlayerSchema = orm.define("players", {
    fields = {uid = "string", level = "int", gold = "int"},
    indexes = {{fields = {"uid"}, unique = true}}
})

-- Typed CRUD
local player = orm.find(PlayerSchema, {uid = "alice"})
orm.insert(PlayerSchema, {uid = "bob", level = 1, gold = 100})
orm.update(PlayerSchema, {uid = "bob", level = 2})

-- Cache stats
print(orm.cache_stats())  -- {hits = 1234, misses = 56, hit_rate = 0.956}
```

### Step 5: Tests

- Cache hit/miss behavior
- LRU eviction
- Write-through consistency
- Concurrent access (multiple coroutines reading/writing)

## Acceptance Criteria

1. OrmSession provides typed CRUD for registered schemas
2. EntityCache provides LRU caching with configurable size
3. Write operations invalidate/update cache correctly
4. Cache hit rate is measurable
5. Lua API exposes orm.define, orm.find/insert/update/delete
6. Tests verify cache consistency

## Dependencies

- P1-6 (DB Backpressure) — queue monitoring

## Estimated Effort: ~800 lines
