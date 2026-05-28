#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>

#include "runtime/database/cache.h"

using namespace engine::database;

// ============================================================================
// EntityCache: construction and initial state
// ============================================================================

TEST_CASE("EntityCache default construction", "[cache][lifecycle]") {
    EntityCache<std::string> cache;
    REQUIRE(cache.Size() == 0);
    REQUIRE(cache.HitCount() == 0);
    REQUIRE(cache.MissCount() == 0);
    REQUIRE(cache.EvictCount() == 0);
}

TEST_CASE("EntityCache with custom capacity", "[cache][lifecycle]") {
    EntityCache<std::string> cache(5);
    REQUIRE(cache.Size() == 0);
}

TEST_CASE("EntityCache supports various value types", "[cache][lifecycle]") {
    EntityCache<int> int_cache;
    EntityCache<double> double_cache;
    EntityCache<std::string> str_cache;

    REQUIRE(int_cache.Size() == 0);
    REQUIRE(double_cache.Size() == 0);
    REQUIRE(str_cache.Size() == 0);
}

// ============================================================================
// EntityCache: Put and Get
// ============================================================================

TEST_CASE("Put then Get returns inserted value", "[cache][basic]") {
    EntityCache<std::string> cache;
    cache.Put("key1", "value1");

    auto result = cache.Get("key1");
    REQUIRE(result.has_value());
    REQUIRE(*result == "value1");
}

TEST_CASE("Put overwrites existing key", "[cache][basic]") {
    EntityCache<std::string> cache;
    cache.Put("key", "old");
    cache.Put("key", "new");

    auto result = cache.Get("key");
    REQUIRE(result.has_value());
    REQUIRE(*result == "new");
    REQUIRE(cache.Size() == 1);  // no duplicate entries
}

TEST_CASE("Put updates LRU order on overwrite", "[cache][basic]") {
    EntityCache<std::string> cache(3);

    cache.Put("a", "a_val");
    cache.Put("b", "b_val");
    cache.Put("c", "c_val");

    // Access 'a' to promote it. Then overwrite 'b'.
    cache.Get("a");
    cache.Put("b", "b_updated");

    // 'c' should still be present
    auto result = cache.Get("c");
    REQUIRE(result.has_value());
    REQUIRE(*result == "c_val");
}

// ============================================================================
// EntityCache: Get misses
// ============================================================================

TEST_CASE("Get on empty cache returns nullopt", "[cache][miss]") {
    EntityCache<std::string> cache;
    auto result = cache.Get("nonexistent");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Get on missing key returns nullopt", "[cache][miss]") {
    EntityCache<std::string> cache;
    cache.Put("exists", "value");

    auto result = cache.Get("missing");
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// EntityCache: Invalidate
// ============================================================================

TEST_CASE("Invalidate removes existing entry", "[cache][invalidate]") {
    EntityCache<std::string> cache;
    cache.Put("removable", "val");

    REQUIRE(cache.Size() == 1);
    cache.Invalidate("removable");

    REQUIRE(cache.Size() == 0);
    auto result = cache.Get("removable");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Invalidate on missing key is a no-op", "[cache][invalidate]") {
    EntityCache<std::string> cache;
    cache.Put("keep", "val");

    REQUIRE_NOTHROW(cache.Invalidate("no_such_key"));
    REQUIRE(cache.Size() == 1);  // unchanged
}

TEST_CASE("Invalidate all entries one by one", "[cache][invalidate]") {
    EntityCache<std::string> cache;
    cache.Put("a", "1");
    cache.Put("b", "2");
    cache.Put("c", "3");
    REQUIRE(cache.Size() == 3);

    cache.Invalidate("a");
    cache.Invalidate("b");
    cache.Invalidate("c");

    REQUIRE(cache.Size() == 0);
}

// ============================================================================
// EntityCache: Clear
// ============================================================================

TEST_CASE("Clear empties non-empty cache", "[cache][clear]") {
    EntityCache<std::string> cache;
    cache.Put("a", "1");
    cache.Put("b", "2");
    cache.Put("c", "3");

    REQUIRE(cache.Size() == 3);
    cache.Clear();
    REQUIRE(cache.Size() == 0);
}

TEST_CASE("Clear on already-empty cache is safe", "[cache][clear]") {
    EntityCache<std::string> cache;
    REQUIRE_NOTHROW(cache.Clear());
    REQUIRE(cache.Size() == 0);
}

// ============================================================================
// EntityCache: LRU eviction
// ============================================================================

TEST_CASE("Eviction when capacity exceeded", "[cache][eviction]") {
    EntityCache<int> cache(3);

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);
    REQUIRE(cache.Size() == 3);
    REQUIRE(cache.EvictCount() == 0);

    // This should evict the LRU entry (key "a")
    cache.Put("d", 4);
    REQUIRE(cache.Size() == 3);
    REQUIRE(cache.EvictCount() == 1);

    // "a" is evicted
    REQUIRE_FALSE(cache.Get("a").has_value());
    // "b", "c", "d" remain
    REQUIRE(cache.Get("b").has_value());
    REQUIRE(cache.Get("c").has_value());
    REQUIRE(cache.Get("d").has_value());
}

TEST_CASE("Eviction evicts least-recently-used entry", "[cache][eviction]") {
    EntityCache<std::string> cache(2);

    cache.Put("a", "first");
    cache.Put("b", "second");

    // Access "a" so "b" becomes LRU
    cache.Get("a");

    cache.Put("c", "third");  // should evict "b", not "a"

    REQUIRE(cache.Get("a").has_value());   // still present (recently used)
    REQUIRE_FALSE(cache.Get("b").has_value());  // evicted
    REQUIRE(cache.Get("c").has_value());   // newly inserted
}

TEST_CASE("Multiple evictions for large overflow", "[cache][eviction]") {
    EntityCache<int> cache(3);

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);
    REQUIRE(cache.Size() == 3);

    cache.Put("d", 4);  // evicts one
    REQUIRE(cache.EvictCount() == 1);

    cache.Put("e", 5);  // evicts one more
    REQUIRE(cache.EvictCount() == 2);

    cache.Put("f", 6);  // evicts one more
    REQUIRE(cache.EvictCount() == 3);

    REQUIRE(cache.Size() == 3);
}

TEST_CASE("Put existing key does not trigger eviction", "[cache][eviction]") {
    EntityCache<std::string> cache(1);

    cache.Put("only", "v1");
    REQUIRE(cache.Size() == 1);

    cache.Put("only", "v2");  // overwrite, same key
    REQUIRE(cache.Size() == 1);
    REQUIRE(cache.EvictCount() == 0);
}

// ============================================================================
// EntityCache: statistics
// ============================================================================

TEST_CASE("HitCount increments on successful Get", "[cache][stats]") {
    EntityCache<std::string> cache;
    cache.Put("k", "v");

    REQUIRE(cache.HitCount() == 0);
    cache.Get("k");
    REQUIRE(cache.HitCount() == 1);

    cache.Get("k");
    cache.Get("k");
    REQUIRE(cache.HitCount() == 3);
}

TEST_CASE("MissCount increments on failed Get", "[cache][stats]") {
    EntityCache<std::string> cache;

    REQUIRE(cache.MissCount() == 0);
    cache.Get("nothing");
    REQUIRE(cache.MissCount() == 1);

    cache.Put("exists", "v");
    cache.Get("still_missing");
    REQUIRE(cache.MissCount() == 2);
}

TEST_CASE("HitRate is 0.0 for empty cache", "[cache][stats]") {
    EntityCache<std::string> cache;
    REQUIRE(cache.HitRate() == 0.0);
}

TEST_CASE("HitRate is 1.0 when all hits", "[cache][stats]") {
    EntityCache<std::string> cache;
    cache.Put("a", "1");
    cache.Put("b", "2");

    cache.Get("a");
    cache.Get("b");
    cache.Get("a");

    REQUIRE(cache.HitRate() == 1.0);
}

TEST_CASE("HitRate is 0.0 when all misses", "[cache][stats]") {
    EntityCache<std::string> cache;
    cache.Get("x");
    cache.Get("y");
    cache.Get("z");

    REQUIRE(cache.HitCount() == 0);
    REQUIRE(cache.MissCount() == 3);
    REQUIRE(cache.HitRate() == 0.0);
}

TEST_CASE("HitRate is 0.5 for even split", "[cache][stats]") {
    EntityCache<std::string> cache;
    cache.Put("hit", "v");

    cache.Get("hit");    // hit
    cache.Get("miss");   // miss

    REQUIRE(cache.HitRate() > 0.49);
    REQUIRE(cache.HitRate() < 0.51);
}

TEST_CASE("EvictCount is zero without eviction", "[cache][stats]") {
    EntityCache<std::string> cache(100);
    cache.Put("a", "1");
    cache.Put("b", "2");
    REQUIRE(cache.EvictCount() == 0);
}

// ============================================================================
// EntityCache: Size tracking
// ============================================================================

TEST_CASE("Size reflects current entry count", "[cache][size]") {
    EntityCache<int> cache;
    REQUIRE(cache.Size() == 0);

    cache.Put("a", 1);
    REQUIRE(cache.Size() == 1);

    cache.Put("b", 2);
    REQUIRE(cache.Size() == 2);

    cache.Invalidate("a");
    REQUIRE(cache.Size() == 1);

    cache.Clear();
    REQUIRE(cache.Size() == 0);
}

// ============================================================================
// EntityCache: capacity of 1 edge case
// ============================================================================

TEST_CASE("Capacity 1: only latest entry retained", "[cache][edge]") {
    EntityCache<std::string> cache(1);
    cache.Put("first", "v1");
    REQUIRE(cache.Size() == 1);
    REQUIRE(cache.Get("first").has_value());

    cache.Put("second", "v2");
    REQUIRE(cache.Size() == 1);
    REQUIRE_FALSE(cache.Get("first").has_value());
    REQUIRE(cache.Get("second").has_value());
}

// ============================================================================
// EntityCache: basic thread safety (single-threaded validation)
// ============================================================================

TEST_CASE("Cache is accessible after move-like usage pattern", "[cache][safety]") {
    EntityCache<int> cache(50);
    for (int i = 0; i < 100; i++) {
        cache.Put("k" + std::to_string(i), i);
    }
    REQUIRE(cache.Size() == 50);  // only 50 retained

    // Still functional after heavy insertions
    cache.Put("new", 999);
    auto result = cache.Get("new");
    REQUIRE(result.has_value());
    REQUIRE(*result == 999);
}

// ============================================================================
// EntityCache: statistics unaffected by Invalidate/Clear
// ============================================================================

TEST_CASE("Statistics persist across Invalidate and Clear", "[cache][stats]") {
    EntityCache<std::string> cache;
    cache.Put("a", "1");
    cache.Put("b", "2");

    cache.Get("a");   // hit
    cache.Get("c");   // miss

    size_t hits_before = cache.HitCount();
    size_t misses_before = cache.MissCount();

    cache.Invalidate("a");
    cache.Clear();

    // Statistics are cumulative — not reset by structural changes
    REQUIRE(cache.HitCount() >= hits_before);
    REQUIRE(cache.MissCount() >= misses_before);
}

// ============================================================================
// EntityCache: stress with many keys
// ============================================================================

TEST_CASE("Many keys within capacity", "[cache][stress]") {
    EntityCache<int> cache(1000);
    for (int i = 0; i < 500; i++) {
        cache.Put("key_" + std::to_string(i), i);
    }
    REQUIRE(cache.Size() == 500);
    REQUIRE(cache.EvictCount() == 0);

    // Verify random access
    REQUIRE(cache.Get("key_0").has_value());
    REQUIRE(cache.Get("key_250").has_value());
    REQUIRE(cache.Get("key_499").has_value());
    REQUIRE_FALSE(cache.Get("key_999").has_value());
}
