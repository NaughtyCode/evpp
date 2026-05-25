#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_bulk_operation_t for ordered/unordered bulk writes.
//
// Usage:
//   auto* bulk = MongoBulkOperation::New(true); // ordered
//   bulk->InsertOne(doc);
//   bulk->UpdateOne(selector, update);
//   uint32_t server_id = bulk->Execute(reply, &error);
//   delete bulk;
class ENGINE_API MongoBulkOperation {
public:
    static MongoBulkOperation* New(bool ordered);

    void Destroy();

    // ── Add operations ──────────────────────────────────────────────
    // Legacy-style (no error parameter, no opts)
    void Insert(const BsonDocument& document);
    void Remove(const BsonDocument& selector);
    void RemoveOne(const BsonDocument& selector);
    void ReplaceOne(const BsonDocument& selector, const BsonDocument& document, bool upsert);
    void Update(const BsonDocument& selector, const BsonDocument& document, bool upsert);
    void UpdateOne(const BsonDocument& selector, const BsonDocument& document, bool upsert);

    // Opts-style (with optional BSON opts and error out-parameter)
    bool InsertWithOpts(const BsonDocument& document, const BsonDocument* opts, MongoError* error);
    bool RemoveOneWithOpts(const BsonDocument& selector, const BsonDocument* opts, MongoError* error);
    bool RemoveManyWithOpts(const BsonDocument& selector, const BsonDocument* opts, MongoError* error);
    bool ReplaceOneWithOpts(const BsonDocument& selector, const BsonDocument& document,
                            const BsonDocument* opts, MongoError* error);
    bool UpdateOneWithOpts(const BsonDocument& selector, const BsonDocument& document,
                           const BsonDocument* opts, MongoError* error);
    bool UpdateManyWithOpts(const BsonDocument& selector, const BsonDocument& document,
                            const BsonDocument* opts, MongoError* error);

    // ── Execute ─────────────────────────────────────────────────────
    // Returns server_id on success; 0 on error (check error out-parameter).
    uint32_t Execute(BsonDocument* reply, MongoError* error);

    // ── Settings ────────────────────────────────────────────────────
    void SetBypassDocumentValidation(bool bypass);
    void SetLet(const BsonDocument& let);
    void SetWriteConcern(const MongoWriteConcern& write_concern);
    void SetServerId(uint32_t server_id);
    uint32_t GetServerId() const;
    void SetDatabase(const char* database);
    void SetCollection(const char* collection);

    void* RawBulkOperation(); // returns mongoc_bulk_operation_t*
    void SetRawBulkOperation(void* bulk); // takes ownership

private:
    friend class MongoCollection;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoBulkOperation();
    ~MongoBulkOperation();
    MongoBulkOperation(const MongoBulkOperation&) = delete;
    MongoBulkOperation& operator=(const MongoBulkOperation&) = delete;
    MongoBulkOperation(MongoBulkOperation&&) = delete;
    MongoBulkOperation& operator=(MongoBulkOperation&&) = delete;
};

} // namespace mongo
} // namespace engine
