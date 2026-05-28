#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

#include <memory>
#include <string>
#include <vector>

#include "log_init.h"
#include "runtime/database/cache.h"
#include "runtime/database/orm.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"

using namespace engine::entity;
using namespace engine::database;

// ============================================================================
// Integration: ORM + Entity — schema-based persistence with caching
// ============================================================================

TEST_CASE("EntityCache stores and retrieves entity data", "[integration][orm][entity]") {
    EntityCache<std::string> cache(100);

    // Cache miss
    auto result = cache.Get("player:1");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(cache.MissCount() == 1);
    REQUIRE(cache.HitCount() == 0);

    // Insert and hit
    cache.Put("player:1", R"({"name":"Hero","hp":100})");
    auto cached = cache.Get("player:1");
    REQUIRE(cached.has_value());
    REQUIRE(cached.value() == R"({"name":"Hero","hp":100})");
    REQUIRE(cache.HitCount() == 1);
}

TEST_CASE("EntityCache LRU eviction removes oldest entries", "[integration][orm][entity]") {
    EntityCache<std::string> cache(3);  // max 3 entries

    cache.Put("a", "data-a");
    cache.Put("b", "data-b");
    cache.Put("c", "data-c");

    // Access "a" to make it recently used
    cache.Get("a");

    // Insert "d" — should evict "b" (oldest)
    cache.Put("d", "data-d");

    REQUIRE(cache.Get("a").has_value());  // recently used, kept
    REQUIRE_FALSE(cache.Get("b").has_value());  // oldest, evicted
    REQUIRE(cache.Get("c").has_value());  // kept
    REQUIRE(cache.Get("d").has_value());  // newly inserted
    REQUIRE(cache.EvictCount() == 1);
}

TEST_CASE("EntityCache hit rate calculation", "[integration][orm][entity]") {
    EntityCache<int> cache(10);

    cache.Put("k1", 1);
    cache.Put("k2", 2);

    cache.Get("k1");  // hit
    cache.Get("k2");  // hit
    cache.Get("k3");  // miss

    REQUIRE(cache.HitCount() == 2);
    REQUIRE(cache.MissCount() == 1);
    REQUIRE(cache.HitRate() == Approx(2.0 / 3.0));
}

TEST_CASE("EntityCache invalidate removes entry", "[integration][orm][entity]") {
    EntityCache<std::string> cache(10);

    cache.Put("player:1", "data");
    REQUIRE(cache.Get("player:1").has_value());

    cache.Invalidate("player:1");
    REQUIRE_FALSE(cache.Get("player:1").has_value());
}

TEST_CASE("OrmSession registers schema and creates cache", "[integration][orm][entity]") {
    auto& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "test_players";
    schema.fields.push_back({"name", FieldType::kString});
    schema.fields.push_back({"hp", FieldType::kInt});

    session.RegisterSchema(schema);

    const auto* retrieved = session.GetSchema("test_players");
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->collection_name == "test_players");
    REQUIRE(retrieved->fields.size() == 2);

    // Cache is automatically created
    auto* cache = session.GetCache("test_players");
    REQUIRE(cache != nullptr);
}

TEST_CASE("OrmSession cache integration with entity data", "[integration][orm][entity]") {
    auto& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "cache_test";

    session.RegisterSchema(schema);
    auto* cache = session.GetCache("cache_test");
    REQUIRE(cache != nullptr);

    // Simulate entity data caching
    cache->Put("entity:1", R"({"name":"Warrior","level":5})");
    cache->Put("entity:2", R"({"name":"Mage","level":3})");

    auto e1 = cache->Get("entity:1");
    REQUIRE(e1.has_value());
    REQUIRE(e1.value().find("Warrior") != std::string::npos);

    auto e2 = cache->Get("entity:2");
    REQUIRE(e2.has_value());
    REQUIRE(e2.value().find("Mage") != std::string::npos);

    // Cache hit rate should be 100%
    REQUIRE(cache->HitRate() == Approx(1.0));
}

TEST_CASE("OrmSession ClearAllCaches resets all caches", "[integration][orm][entity]") {
    auto& session = OrmSession::Instance();

    session.RegisterSchema({"schema_a"});
    session.RegisterSchema({"schema_b"});

    auto* cache_a = session.GetCache("schema_a");
    auto* cache_b = session.GetCache("schema_b");
    REQUIRE(cache_a != nullptr);
    REQUIRE(cache_b != nullptr);

    cache_a->Put("k1", "v1");
    cache_b->Put("k2", "v2");
    REQUIRE(cache_a->Size() == 1);
    REQUIRE(cache_b->Size() == 1);

    session.ClearAllCaches();
    REQUIRE(cache_a->Size() == 0);
    REQUIRE(cache_b->Size() == 0);
}
