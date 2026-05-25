#include "runtime/database/mongo/mongo_error.h"

#include <bson/bson.h>
#include <cstring>

namespace engine {
namespace mongo {

static_assert(sizeof(MongoError) >= sizeof(bson_error_t),
              "MongoError must be >= bson_error_t size");

MongoError::MongoError() {
    std::memset(storage_, 0, sizeof(storage_));
}

MongoError::~MongoError() = default;

MongoError::MongoError(MongoError&& other) noexcept {
    std::memcpy(storage_, other.storage_, sizeof(storage_));
    std::memset(other.storage_, 0, sizeof(other.storage_));
}

MongoError& MongoError::operator=(MongoError&& other) noexcept {
    if (this != &other) {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        std::memset(other.storage_, 0, sizeof(other.storage_));
    }
    return *this;
}

void MongoError::Clear() {
    auto* e = static_cast<bson_error_t*>(RawError());
    e->domain = 0;
    e->code = 0;
    e->message[0] = '\0';
}

uint32_t MongoError::Domain() const {
    return static_cast<const bson_error_t*>(RawError())->domain;
}

uint32_t MongoError::Code() const {
    return static_cast<const bson_error_t*>(RawError())->code;
}

const char* MongoError::Message() const {
    return static_cast<const bson_error_t*>(RawError())->message;
}

void* MongoError::RawError() {
    return static_cast<void*>(storage_);
}

const void* MongoError::RawError() const {
    return static_cast<const void*>(storage_);
}

} // namespace mongo
} // namespace engine
