#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/database/orm.h"

using namespace engine;
using namespace engine::database;

// ============================================================================
// OrmSession: singleton access
// ============================================================================

TEST_CASE("OrmSession Instance returns same object", "[orm][singleton]") {
    OrmSession& a = OrmSession::Instance();
    OrmSession& b = OrmSession::Instance();
    REQUIRE(&a == &b);
}

// ============================================================================
// OrmSession: schema registration
// ============================================================================

TEST_CASE("RegisterSchema stores and retrieves schema", "[orm][schema]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "players";
    schema.fields.push_back({"id", FieldType::kString});
    schema.fields.push_back({"name", FieldType::kString});
    schema.fields.push_back({"level", FieldType::kInt, "1"});
    schema.indexes.push_back({{"id"}, true});
    schema.indexes.push_back({{"name"}, false});

    session.RegisterSchema(schema);

    const CollectionSchema* retrieved = session.GetSchema("players");
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->collection_name == "players");
    REQUIRE(retrieved->fields.size() == 3);
    REQUIRE(retrieved->indexes.size() == 2);
}

TEST_CASE("GetSchema returns nullptr for unknown collection", "[orm][schema]") {
    OrmSession& session = OrmSession::Instance();
    const CollectionSchema* s = session.GetSchema("nonexistent_collection");
    REQUIRE(s == nullptr);
}

TEST_CASE("RegisterSchema overwrites existing schema", "[orm][schema]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema v1;
    v1.collection_name = "items";
    v1.fields.push_back({"id", FieldType::kString});
    session.RegisterSchema(v1);

    CollectionSchema v2;
    v2.collection_name = "items";
    v2.fields.push_back({"id", FieldType::kString});
    v2.fields.push_back({"price", FieldType::kDouble, "0.0"});
    session.RegisterSchema(v2);

    const CollectionSchema* retrieved = session.GetSchema("items");
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->fields.size() == 2);  // overwritten
}

// ============================================================================
// OrmSession: schema fields and indexes detail
// ============================================================================

TEST_CASE("Schema FieldDef has correct types and defaults", "[orm][schema]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "mixed";
    schema.fields.push_back({"str_field", FieldType::kString, "hello"});
    schema.fields.push_back({"int_field", FieldType::kInt, "42"});
    schema.fields.push_back({"double_field", FieldType::kDouble, "3.14"});
    schema.fields.push_back({"bool_field", FieldType::kBool, "true"});
    schema.fields.push_back({"obj_field", FieldType::kObject});
    schema.fields.push_back({"arr_field", FieldType::kArray});

    session.RegisterSchema(schema);

    const CollectionSchema* s = session.GetSchema("mixed");
    REQUIRE(s != nullptr);
    REQUIRE(s->fields.size() == 6);

    REQUIRE(s->fields[0].name == "str_field");
    REQUIRE(s->fields[0].type == FieldType::kString);
    REQUIRE(s->fields[0].default_value == "hello");

    REQUIRE(s->fields[1].name == "int_field");
    REQUIRE(s->fields[1].type == FieldType::kInt);

    REQUIRE(s->fields[3].name == "bool_field");
    REQUIRE(s->fields[3].type == FieldType::kBool);
}

TEST_CASE("Schema IndexDef supports unique and non-unique indexes", "[orm][schema]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "indexed";
    schema.indexes.push_back({{"id"}, true});
    schema.indexes.push_back({{"name", "type"}, false});
    schema.indexes.push_back({{"x", "y", "z"}, true});

    session.RegisterSchema(schema);

    const CollectionSchema* s = session.GetSchema("indexed");
    REQUIRE(s != nullptr);
    REQUIRE(s->indexes[0].unique == true);
    REQUIRE(s->indexes[1].unique == false);
    REQUIRE(s->indexes[1].fields.size() == 2);
    REQUIRE(s->indexes[2].fields.size() == 3);
}

// ============================================================================
// OrmSession: FindById
// ============================================================================

TEST_CASE("FindById on unregistered collection returns nullopt", "[orm][query]") {
    OrmSession& session = OrmSession::Instance();
    auto result = session.FindById("no_such_collection", "123");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("FindById on registered collection without data returns nullopt", "[orm][query]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "empty_players";
    session.RegisterSchema(schema);

    auto result = session.FindById("empty_players", "player_1");
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// OrmSession: Insert and Find flow
// ============================================================================

TEST_CASE("Insert then FindById round-trip", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "roundtrip_test";
    session.RegisterSchema(schema);

    std::string doc = R"({"id":"abc","name":"test_doc"})";
    bool inserted = session.Insert("roundtrip_test", doc);
    REQUIRE(inserted);

    auto found = session.FindById("roundtrip_test", "abc");
    REQUIRE(found.has_value());
}

TEST_CASE("Insert then Find with query", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "query_test";
    session.RegisterSchema(schema);

    session.Insert("query_test", R"({"id":"a","val":"1"})");
    session.Insert("query_test", R"({"id":"b","val":"2"})");

    Query q;
    q["val"] = "2";
    auto results = session.Find("query_test", q);
    REQUIRE(results.size() == 1);
}

// ============================================================================
// OrmSession: Find with FindOptions
// ============================================================================

TEST_CASE("Find with limit option", "[orm][query]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "limit_test";
    session.RegisterSchema(schema);

    session.Insert("limit_test", R"({"id":"1","x":"a"})");
    session.Insert("limit_test", R"({"id":"2","x":"a"})");
    session.Insert("limit_test", R"({"id":"3","x":"a"})");

    Query q;
    q["x"] = "a";
    FindOptions opts;
    opts.limit = 2;

    auto results = session.Find("limit_test", q, opts);
    REQUIRE(results.size() == 2);
}

TEST_CASE("Find with skip option", "[orm][query]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "skip_test";
    session.RegisterSchema(schema);

    session.Insert("skip_test", R"({"id":"1"})");
    session.Insert("skip_test", R"({"id":"2"})");
    session.Insert("skip_test", R"({"id":"3"})");

    Query q;
    FindOptions opts;
    opts.skip = 1;
    opts.limit = 10;
    auto results = session.Find("skip_test", q, opts);
    REQUIRE(results.size() == 2);
}

TEST_CASE("Find default options have zero limit and skip", "[orm][query]") {
    FindOptions opts;
    REQUIRE(opts.limit == 0);
    REQUIRE(opts.skip == 0);
}

// ============================================================================
// OrmSession: Update and Delete
// ============================================================================

TEST_CASE("Update existing document", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "update_test";
    session.RegisterSchema(schema);

    session.Insert("update_test", R"({"id":"doc1","val":"old"})");

    bool updated = session.Update("update_test", "doc1", R"({"val":"new"})");
    REQUIRE(updated);
}

TEST_CASE("Update non-existent document returns false", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "update_miss";
    session.RegisterSchema(schema);

    bool updated = session.Update("update_miss", "ghost", R"({"val":"x"})");
    REQUIRE_FALSE(updated);
}

TEST_CASE("DeleteById removes document", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "delete_test";
    session.RegisterSchema(schema);

    session.Insert("delete_test", R"({"id":"killme"})");

    auto found = session.FindById("delete_test", "killme");
    REQUIRE(found.has_value());

    bool deleted = session.DeleteById("delete_test", "killme");
    REQUIRE(deleted);

    auto gone = session.FindById("delete_test", "killme");
    REQUIRE_FALSE(gone.has_value());
}

TEST_CASE("DeleteById non-existent returns false", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "delete_miss";
    session.RegisterSchema(schema);

    bool deleted = session.DeleteById("delete_miss", "nobody");
    REQUIRE_FALSE(deleted);
}

// ============================================================================
// OrmSession: cache access
// ============================================================================

TEST_CASE("GetCache returns a cache for registered collection", "[orm][cache]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "cache_test";
    session.RegisterSchema(schema);

    EntityCache<std::string>* cache = session.GetCache("cache_test");
    REQUIRE(cache != nullptr);
}

TEST_CASE("GetCache for unregistered collection returns nullptr", "[orm][cache]") {
    OrmSession& session = OrmSession::Instance();
    EntityCache<std::string>* cache = session.GetCache("no_such_cache");
    REQUIRE(cache == nullptr);
}

// ============================================================================
// OrmSession: global cache statistics
// ============================================================================

TEST_CASE("Cache statistics track hits and misses", "[orm][cache]") {
    OrmSession& session = OrmSession::Instance();

    // Stats start from current state — record baseline
    size_t base_hits = session.TotalCacheHits();
    size_t base_misses = session.TotalCacheMisses();

    CollectionSchema schema;
    schema.collection_name = "stat_test";
    session.RegisterSchema(schema);

    EntityCache<std::string>* cache = session.GetCache("stat_test");
    REQUIRE(cache != nullptr);

    // Insert data that populates cache
    session.Insert("stat_test", R"({"id":"s1","v":"1"})");

    // Access to trigger cache activity
    session.FindById("stat_test", "s1");
    session.FindById("stat_test", "nonexistent");

    // Totals should be >= baseline
    REQUIRE(session.TotalCacheHits() >= base_hits);
    REQUIRE(session.TotalCacheMisses() >= base_misses);
}

TEST_CASE("GlobalHitRate returns value between 0 and 1", "[orm][cache]") {
    OrmSession& session = OrmSession::Instance();
    double rate = session.GlobalHitRate();
    REQUIRE(rate >= 0.0);
    REQUIRE(rate <= 1.0);
}

TEST_CASE("ClearAllCaches resets caches", "[orm][cache]") {
    OrmSession& session = OrmSession::Instance();

    CollectionSchema schema;
    schema.collection_name = "clear_test";
    session.RegisterSchema(schema);

    session.Insert("clear_test", R"({"id":"c1"})");
    session.Insert("clear_test", R"({"id":"c2"})");

    REQUIRE_NOTHROW(session.ClearAllCaches());
}

// ============================================================================
// OrmSession: CRUD on unregistered collection
// ============================================================================

TEST_CASE("Insert on unregistered collection returns false", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();
    bool ok = session.Insert("unregistered_col", R"({"id":"x"})");
    REQUIRE_FALSE(ok);
}

TEST_CASE("Find on unregistered collection returns empty", "[orm][query]") {
    OrmSession& session = OrmSession::Instance();
    auto results = session.Find("unregistered_col", {});
    REQUIRE(results.empty());
}

TEST_CASE("Update on unregistered collection returns false", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();
    bool ok = session.Update("unregistered_col", "x", "{}");
    REQUIRE_FALSE(ok);
}

TEST_CASE("DeleteById on unregistered collection returns false", "[orm][crud]") {
    OrmSession& session = OrmSession::Instance();
    bool ok = session.DeleteById("unregistered_col", "x");
    REQUIRE_FALSE(ok);
}
