#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Per-operation options for bulk write InsertOne.
class ENGINE_API MongoBulkWriteInsertOneOpts {
public:
    MongoBulkWriteInsertOneOpts();
    ~MongoBulkWriteInsertOneOpts();
    MongoBulkWriteInsertOneOpts(const MongoBulkWriteInsertOneOpts&) = delete;
    MongoBulkWriteInsertOneOpts& operator=(const MongoBulkWriteInsertOneOpts&) = delete;
    MongoBulkWriteInsertOneOpts(MongoBulkWriteInsertOneOpts&&) noexcept;
    MongoBulkWriteInsertOneOpts& operator=(MongoBulkWriteInsertOneOpts&&) noexcept;

    void* Raw(); // returns mongoc_bulkwrite_insertoneopts_t*
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-operation options for bulk write UpdateOne.
class ENGINE_API MongoBulkWriteUpdateOneOpts {
public:
    MongoBulkWriteUpdateOneOpts();
    ~MongoBulkWriteUpdateOneOpts();
    MongoBulkWriteUpdateOneOpts(const MongoBulkWriteUpdateOneOpts&) = delete;
    MongoBulkWriteUpdateOneOpts& operator=(const MongoBulkWriteUpdateOneOpts&) = delete;
    MongoBulkWriteUpdateOneOpts(MongoBulkWriteUpdateOneOpts&&) noexcept;
    MongoBulkWriteUpdateOneOpts& operator=(MongoBulkWriteUpdateOneOpts&&) noexcept;

    void SetArrayFilters(const BsonDocument& array_filters);
    void SetCollation(const BsonDocument& collation);
    // hint is a raw bson_value_t*
    void SetHint(const void* hint);
    void SetUpsert(bool upsert);
    void SetSort(const BsonDocument& sort);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-operation options for bulk write UpdateMany.
class ENGINE_API MongoBulkWriteUpdateManyOpts {
public:
    MongoBulkWriteUpdateManyOpts();
    ~MongoBulkWriteUpdateManyOpts();
    MongoBulkWriteUpdateManyOpts(const MongoBulkWriteUpdateManyOpts&) = delete;
    MongoBulkWriteUpdateManyOpts& operator=(const MongoBulkWriteUpdateManyOpts&) = delete;
    MongoBulkWriteUpdateManyOpts(MongoBulkWriteUpdateManyOpts&&) noexcept;
    MongoBulkWriteUpdateManyOpts& operator=(MongoBulkWriteUpdateManyOpts&&) noexcept;

    void SetArrayFilters(const BsonDocument& array_filters);
    void SetCollation(const BsonDocument& collation);
    void SetHint(const void* hint);
    void SetUpsert(bool upsert);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-operation options for bulk write ReplaceOne.
class ENGINE_API MongoBulkWriteReplaceOneOpts {
public:
    MongoBulkWriteReplaceOneOpts();
    ~MongoBulkWriteReplaceOneOpts();
    MongoBulkWriteReplaceOneOpts(const MongoBulkWriteReplaceOneOpts&) = delete;
    MongoBulkWriteReplaceOneOpts& operator=(const MongoBulkWriteReplaceOneOpts&) = delete;
    MongoBulkWriteReplaceOneOpts(MongoBulkWriteReplaceOneOpts&&) noexcept;
    MongoBulkWriteReplaceOneOpts& operator=(MongoBulkWriteReplaceOneOpts&&) noexcept;

    void SetCollation(const BsonDocument& collation);
    void SetHint(const void* hint);
    void SetUpsert(bool upsert);
    void SetSort(const BsonDocument& sort);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-operation options for bulk write DeleteOne.
class ENGINE_API MongoBulkWriteDeleteOneOpts {
public:
    MongoBulkWriteDeleteOneOpts();
    ~MongoBulkWriteDeleteOneOpts();
    MongoBulkWriteDeleteOneOpts(const MongoBulkWriteDeleteOneOpts&) = delete;
    MongoBulkWriteDeleteOneOpts& operator=(const MongoBulkWriteDeleteOneOpts&) = delete;
    MongoBulkWriteDeleteOneOpts(MongoBulkWriteDeleteOneOpts&&) noexcept;
    MongoBulkWriteDeleteOneOpts& operator=(MongoBulkWriteDeleteOneOpts&&) noexcept;

    void SetCollation(const BsonDocument& collation);
    void SetHint(const void* hint);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-operation options for bulk write DeleteMany.
class ENGINE_API MongoBulkWriteDeleteManyOpts {
public:
    MongoBulkWriteDeleteManyOpts();
    ~MongoBulkWriteDeleteManyOpts();
    MongoBulkWriteDeleteManyOpts(const MongoBulkWriteDeleteManyOpts&) = delete;
    MongoBulkWriteDeleteManyOpts& operator=(const MongoBulkWriteDeleteManyOpts&) = delete;
    MongoBulkWriteDeleteManyOpts(MongoBulkWriteDeleteManyOpts&&) noexcept;
    MongoBulkWriteDeleteManyOpts& operator=(MongoBulkWriteDeleteManyOpts&&) noexcept;

    void SetCollation(const BsonDocument& collation);
    void SetHint(const void* hint);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Options for bulk write execution.
class ENGINE_API MongoBulkWriteOpts {
public:
    MongoBulkWriteOpts();
    ~MongoBulkWriteOpts();
    MongoBulkWriteOpts(const MongoBulkWriteOpts&) = delete;
    MongoBulkWriteOpts& operator=(const MongoBulkWriteOpts&) = delete;
    MongoBulkWriteOpts(MongoBulkWriteOpts&&) noexcept;
    MongoBulkWriteOpts& operator=(MongoBulkWriteOpts&&) noexcept;

    void SetOrdered(bool ordered);
    void SetBypassDocumentValidation(bool bypass);
    void SetLet(const BsonDocument& let);
    void SetWriteConcern(const MongoWriteConcern& write_concern);
    // comment is a raw bson_value_t*
    void SetComment(const void* comment);
    void SetVerboseResults(bool verbose);
    void SetExtra(const BsonDocument& extra);
    void SetServerId(uint32_t server_id);

    void* Raw();
    const void* Raw() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Result of a successful bulk write.
class ENGINE_API MongoBulkWriteResult {
public:
    MongoBulkWriteResult();
    ~MongoBulkWriteResult();
    MongoBulkWriteResult(const MongoBulkWriteResult&) = delete;
    MongoBulkWriteResult& operator=(const MongoBulkWriteResult&) = delete;
    MongoBulkWriteResult(MongoBulkWriteResult&&) noexcept;
    MongoBulkWriteResult& operator=(MongoBulkWriteResult&&) noexcept;

    int64_t InsertedCount() const;
    int64_t UpsertedCount() const;
    int64_t MatchedCount() const;
    int64_t ModifiedCount() const;
    int64_t DeletedCount() const;
    const void* InsertResults() const;   // returns const bson_t*
    const void* UpdateResults() const;   // returns const bson_t*
    const void* DeleteResults() const;   // returns const bson_t*
    uint32_t ServerId() const;

    void* Raw();
    void SetRaw(void* raw); // takes ownership

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Exception / error from a bulk write.
class ENGINE_API MongoBulkWriteException {
public:
    MongoBulkWriteException();
    ~MongoBulkWriteException();
    MongoBulkWriteException(const MongoBulkWriteException&) = delete;
    MongoBulkWriteException& operator=(const MongoBulkWriteException&) = delete;
    MongoBulkWriteException(MongoBulkWriteException&&) noexcept;
    MongoBulkWriteException& operator=(MongoBulkWriteException&&) noexcept;

    bool Error(MongoError* error) const;
    const void* WriteErrors() const;          // returns const bson_t*
    const void* WriteConcernErrors() const;   // returns const bson_t*
    const void* ErrorReply() const;           // returns const bson_t*

    void* Raw();
    void SetRaw(void* raw);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Return value from mongoc_bulkwrite_execute: a result and/or exception.
struct ENGINE_API MongoBulkWriteReturn {
    MongoBulkWriteResult* result = nullptr;       // may be null
    MongoBulkWriteException* exception = nullptr; // may be null (no error)
};

// Check result for acknowledged writes.
struct ENGINE_API MongoBulkWriteCheckAcknowledged {
    bool is_ok = false;
    bool is_acknowledged = false;
};

// Server ID after execution.
struct ENGINE_API MongoBulkWriteServerId {
    bool is_ok = false;
    uint32_t server_id = 0;
};

// New-style bulk write (per-mongoc-bulkwrite.h for MongoDB 8.0+).
// Builds a list of write models and executes them in a single command.
//
// Usage:
//   auto* bw = MongoBulkWrite::New(client);
//   bw->AppendInsertOne("db.coll", doc, nullptr, &error);
//   bw->AppendUpdateOne("db.coll", filter, update, &update_opts, &error);
//   auto ret = bw->Execute(&opts);
//   if (ret.exception) { ... }
//   delete bw;
class ENGINE_API MongoBulkWrite {
public:
    static MongoBulkWrite* New(void* raw_client); // raw_client is mongoc_client_t*
    static MongoBulkWrite* New();                  // standalone, use SetClient before execute

    void Destroy();

    MongoBulkWrite(const MongoBulkWrite&) = delete;
    MongoBulkWrite& operator=(const MongoBulkWrite&) = delete;
    MongoBulkWrite(MongoBulkWrite&&) = delete;
    MongoBulkWrite& operator=(MongoBulkWrite&&) = delete;

    // Append operations by namespace string (e.g. "db.collection").
    bool AppendInsertOne(const char* ns, const BsonDocument& document,
                         const MongoBulkWriteInsertOneOpts* opts, MongoError* error);
    bool AppendUpdateOne(const char* ns, const BsonDocument& filter,
                         const BsonDocument& update,
                         const MongoBulkWriteUpdateOneOpts* opts, MongoError* error);
    bool AppendUpdateMany(const char* ns, const BsonDocument& filter,
                          const BsonDocument& update,
                          const MongoBulkWriteUpdateManyOpts* opts, MongoError* error);
    bool AppendReplaceOne(const char* ns, const BsonDocument& filter,
                          const BsonDocument& replacement,
                          const MongoBulkWriteReplaceOneOpts* opts, MongoError* error);
    bool AppendDeleteOne(const char* ns, const BsonDocument& filter,
                         const MongoBulkWriteDeleteOneOpts* opts, MongoError* error);
    bool AppendDeleteMany(const char* ns, const BsonDocument& filter,
                          const MongoBulkWriteDeleteManyOpts* opts, MongoError* error);

    // Execute the bulk write. Returns a struct containing result and/or exception.
    MongoBulkWriteReturn Execute(const MongoBulkWriteOpts* opts);

    // Check whether the previous execute used acknowledged write concern.
    MongoBulkWriteCheckAcknowledged CheckAcknowledged(MongoError* error) const;

    // Get the server ID used by the last execute.
    MongoBulkWriteServerId ServerId(MongoError* error) const;

    void SetSession(void* session);   // mongoc_client_session_t*
    bool SetClient(void* client);    // mongoc_client_t*

    void* Raw();

private:
    friend class MongoClient;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoBulkWrite();
    ~MongoBulkWrite();
};

} // namespace mongo
} // namespace engine

#endif
