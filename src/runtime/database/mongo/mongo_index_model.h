#pragma once

#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_index_model_t for use with MongoCollection::CreateIndexesWithOpts.
class ENGINE_API MongoIndexModel {
public:
    MongoIndexModel(const BsonDocument& keys, const BsonDocument* opts = nullptr);
    ~MongoIndexModel();

    MongoIndexModel(const MongoIndexModel&) = delete;
    MongoIndexModel& operator=(const MongoIndexModel&) = delete;
    MongoIndexModel(MongoIndexModel&&) noexcept;
    MongoIndexModel& operator=(MongoIndexModel&&) noexcept;

    void Destroy();

    void* Raw(); // returns mongoc_index_model_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
