#include "runtime/database/mongo/mongo_index_model.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"

namespace engine {
namespace mongo {

struct MongoIndexModel::Impl {
    mongoc_index_model_t* model = nullptr;
};

MongoIndexModel::MongoIndexModel(const BsonDocument& keys, const BsonDocument* opts)
    : impl_(std::make_unique<Impl>()) {
    impl_->model = mongoc_index_model_new(
        static_cast<const bson_t*>(keys.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
}

MongoIndexModel::~MongoIndexModel() { Destroy(); }

MongoIndexModel::MongoIndexModel(MongoIndexModel&&) noexcept = default;
MongoIndexModel& MongoIndexModel::operator=(MongoIndexModel&& other) noexcept {
    if (this != &other) {
        Destroy();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void MongoIndexModel::Destroy() {
    if (impl_ && impl_->model) {
        mongoc_index_model_destroy(impl_->model);
        impl_->model = nullptr;
    }
}

void* MongoIndexModel::Raw() { return impl_ ? impl_->model : nullptr; }

} // namespace mongo
} // namespace engine
