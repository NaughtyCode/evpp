#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoClient {
public:
    // Create a client from a connection URI string (e.g. "mongodb://localhost:27017").
    static MongoClient* New(const char* uri_string);
    static MongoClient* New(const MongoUri& uri);

    void Destroy(); // frees the underlying mongoc client

    // Settings
    void SetSocketTimeoutMs(int32_t timeout_ms);
    void SetAppname(const char* appname);

    // Get child objects (caller owns the returned pointer; must call Destroy()).
    MongoDatabase* GetDatabase(const char* name);
    MongoDatabase* GetDefaultDatabase();
    MongoCollection* GetCollection(const char* db_name, const char* coll_name);

    // Run a raw command on a database, returning the server reply in `reply`.
    // Returns true on success.
    bool CommandSimple(const char* db_name, const BsonDocument& command,
                       const MongoReadPrefs* read_prefs,
                       BsonDocument* reply, MongoError* error);

    // Internal access
    void* RawClient(); // returns mongoc_client_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    MongoClient();

public:
    ~MongoClient();
    MongoClient(const MongoClient&) = delete;
    MongoClient& operator=(const MongoClient&) = delete;
    MongoClient(MongoClient&&) = delete;
    MongoClient& operator=(MongoClient&&) = delete;
};

class ENGINE_API MongoDatabase {
public:
    void Destroy();

    const char* GetName() const;

    MongoCollection* GetCollection(const char* name);
    MongoCollection* CreateCollection(const char* name, const BsonDocument* options, MongoError* error);

    bool Drop(MongoError* error);
    bool HasCollection(const char* name, MongoError* error);

    bool CommandSimple(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                       BsonDocument* reply, MongoError* error);

    void* RawDatabase(); // returns mongoc_database_t*

private:
    friend class MongoClient;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoDatabase();

public:
    ~MongoDatabase();
    MongoDatabase(const MongoDatabase&) = delete;
    MongoDatabase& operator=(const MongoDatabase&) = delete;
    MongoDatabase(MongoDatabase&&) = delete;
    MongoDatabase& operator=(MongoDatabase&&) = delete;
};

class ENGINE_API MongoCollection {
public:
    void Destroy();

    const char* GetName() const;

    // ── Insert ──────────────────────────────────────────────────────
    bool InsertOne(const BsonDocument& document, const BsonDocument* opts,
                   BsonDocument* reply, MongoError* error);

    // ── Find ────────────────────────────────────────────────────────
    MongoCursor* FindWithOpts(const BsonDocument& filter, const BsonDocument* opts,
                              const MongoReadPrefs* read_prefs);

    // ── Update ──────────────────────────────────────────────────────
    bool UpdateOne(const BsonDocument& selector, const BsonDocument& update,
                   const BsonDocument* opts, BsonDocument* reply, MongoError* error);
    bool UpdateMany(const BsonDocument& selector, const BsonDocument& update,
                    const BsonDocument* opts, BsonDocument* reply, MongoError* error);
    bool ReplaceOne(const BsonDocument& selector, const BsonDocument& replacement,
                    const BsonDocument* opts, BsonDocument* reply, MongoError* error);

    // ── Delete ──────────────────────────────────────────────────────
    bool DeleteOne(const BsonDocument& selector, const BsonDocument* opts,
                   BsonDocument* reply, MongoError* error);
    bool DeleteMany(const BsonDocument& selector, const BsonDocument* opts,
                    BsonDocument* reply, MongoError* error);

    // ── Count ───────────────────────────────────────────────────────
    int64_t CountDocuments(const BsonDocument& filter, const BsonDocument* opts,
                           const MongoReadPrefs* read_prefs,
                           BsonDocument* reply, MongoError* error);

    // ── Drop / indexes ──────────────────────────────────────────────
    bool Drop(MongoError* error);
    bool CreateIndex(const BsonDocument& keys, const BsonDocument* opts,
                     BsonDocument* reply, MongoError* error);

    void* RawCollection(); // returns mongoc_collection_t*

private:
    friend class MongoClient;
    friend class MongoDatabase;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoCollection();

public:
    ~MongoCollection();
    MongoCollection(const MongoCollection&) = delete;
    MongoCollection& operator=(const MongoCollection&) = delete;
    MongoCollection(MongoCollection&&) = delete;
    MongoCollection& operator=(MongoCollection&&) = delete;
};

} // namespace mongo
} // namespace engine
