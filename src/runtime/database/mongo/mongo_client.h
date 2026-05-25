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
    void SetSslOpts(const void* ssl_opts);
    MongoUri GetUri() const;
    void SetReadPrefs(const MongoReadPrefs& read_prefs);
    void SetWriteConcern(const MongoWriteConcern& write_concern);
    void SetReadConcern(const MongoReadConcern& read_concern);
    const void* GetReadPrefs() const;
    const void* GetWriteConcern() const;
    const void* GetReadConcern() const;
    void SetErrorApi(uint32_t version);
    bool SetServerApi(const MongoServerApi& api, MongoError* error);
    void Reset();

    // Get child objects (caller owns the returned pointer; must call Destroy()).
    MongoDatabase* GetDatabase(const char* name);
    MongoDatabase* GetDefaultDatabase();
    MongoCollection* GetCollection(const char* db_name, const char* coll_name);

    // Run a raw command on a database, returning the server reply in `reply`.
    bool CommandSimple(const char* db_name, const BsonDocument& command,
                       const MongoReadPrefs* read_prefs,
                       BsonDocument* reply, MongoError* error);
    bool ReadCommandWithOpts(const char* db_name, const BsonDocument& command,
                             const MongoReadPrefs* read_prefs, const BsonDocument* opts,
                             BsonDocument* reply, MongoError* error);
    bool WriteCommandWithOpts(const char* db_name, const BsonDocument& command,
                              const BsonDocument* opts,
                              BsonDocument* reply, MongoError* error);
    bool ReadWriteCommandWithOpts(const char* db_name, const BsonDocument& command,
                                  const MongoReadPrefs* read_prefs, const BsonDocument* opts,
                                  BsonDocument* reply, MongoError* error);

    // ── Session ────────────────────────────────────────────────────
    MongoSession* StartSession(const MongoSessionOpts* opts, MongoError* error);

    // ── Database names / listing ────────────────────────────────────
    // Caller must bson_free() the returned string array.
    char** GetDatabaseNames(MongoError* error);
    char** GetDatabaseNamesWithOpts(const BsonDocument* opts, MongoError* error);
    MongoCursor* FindDatabasesWithOpts(const BsonDocument* opts);

    // ── Change stream ────────────────────────────────────────────────
    MongoChangeStream* Watch(const BsonDocument& pipeline, const BsonDocument* opts);

    // Internal access
    void* RawClient(); // returns mongoc_client_t*

    // For pool use: release ownership (client is now managed by the pool).
    void ReleaseFromPool();

    // Static factory for pool-owned clients.
    static MongoClient* FromPooled(void* raw_client);

private:
    friend class MongoClientPool;
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
    bool DropWithOpts(const BsonDocument* opts, MongoError* error);
    bool HasCollection(const char* name, MongoError* error);

    bool CommandSimple(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                       BsonDocument* reply, MongoError* error);
    bool ReadCommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                             const BsonDocument* opts, BsonDocument* reply, MongoError* error);
    bool WriteCommandWithOpts(const BsonDocument& command, const BsonDocument* opts,
                              BsonDocument* reply, MongoError* error);
    bool ReadWriteCommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                                  const BsonDocument* opts, BsonDocument* reply, MongoError* error);
    bool CommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                         const BsonDocument* opts, BsonDocument* reply, MongoError* error);

    // ── Read/Write settings ─────────────────────────────────────────
    const void* GetReadPrefs() const;
    void SetReadPrefs(const MongoReadPrefs& read_prefs);
    const void* GetWriteConcern() const;
    void SetWriteConcern(const MongoWriteConcern& write_concern);
    const void* GetReadConcern() const;
    void SetReadConcern(const MongoReadConcern& read_concern);

    // ── Aggregate ──────────────────────────────────────────────────
    MongoCursor* Aggregate(const BsonDocument& pipeline, const BsonDocument* opts,
                           const MongoReadPrefs* read_prefs);

    // ── Change stream ────────────────────────────────────────────────
    MongoChangeStream* Watch(const BsonDocument& pipeline, const BsonDocument* opts);

    // ── Collection names / listing ──────────────────────────────────
    char** GetCollectionNames(MongoError* error);
    char** GetCollectionNamesWithOpts(const BsonDocument* opts, MongoError* error);
    MongoCursor* FindCollectionsWithOpts(const BsonDocument* opts);

    // ── User management ─────────────────────────────────────────────
    bool AddUser(const char* username, const char* password,
                 const BsonDocument* roles, const BsonDocument* custom_data, MongoError* error);
    bool RemoveUser(const char* username, MongoError* error);
    bool RemoveAllUsers(MongoError* error);

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

    // ── Read/Write settings ─────────────────────────────────────────
    const void* GetReadPrefs() const;
    void SetReadPrefs(const MongoReadPrefs& read_prefs);
    const void* GetReadConcern() const;
    void SetReadConcern(const MongoReadConcern& read_concern);
    const void* GetWriteConcern() const;
    void SetWriteConcern(const MongoWriteConcern& write_concern);

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

    // ── Aggregate ──────────────────────────────────────────────────
    MongoCursor* Aggregate(const BsonDocument& pipeline, const BsonDocument* opts,
                           const MongoReadPrefs* read_prefs);

    // ── Insert many ─────────────────────────────────────────────────
    bool InsertMany(const BsonDocument* documents[], size_t count,
                    const BsonDocument* opts, BsonDocument* reply, MongoError* error);

    // ── Find and modify ────────────────────────────────────────────
    bool FindAndModify(const BsonDocument& query, const MongoFindAndModifyOpts* opts,
                       BsonDocument* reply, MongoError* error);

    // ── Change stream ────────────────────────────────────────────────
    MongoChangeStream* Watch(const BsonDocument& pipeline, const BsonDocument* opts);

    // ── Drop / indexes ──────────────────────────────────────────────
    bool Drop(MongoError* error);
    bool DropWithOpts(const BsonDocument* opts, MongoError* error);
    bool DropIndex(const char* index_name, MongoError* error);
    bool DropIndexWithOpts(const char* index_name, const BsonDocument* opts, MongoError* error);
    bool CreateIndex(const BsonDocument& keys, const BsonDocument* opts,
                     BsonDocument* reply, MongoError* error);
    MongoCursor* FindIndexes(const BsonDocument* opts);

    // ── Utilities ───────────────────────────────────────────────────
    char* KeysToIndexString() const;

    // ── Rename ──────────────────────────────────────────────────────
    bool Rename(const char* new_db, const char* new_name, bool drop_target_before_rename,
                MongoError* error);

    // ── Estimated count ─────────────────────────────────────────────
    int64_t EstimatedDocumentCount(const BsonDocument* opts, const MongoReadPrefs* read_prefs,
                                    MongoError* error);

    // ── Bulk operation ─────────────────────────────────────────────
    MongoBulkOperation* CreateBulkOperation(bool ordered, const void* session_raw);

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
