#include "runtime/database/mongo/mongo_optional.h"

#include <mongoc/mongoc.h>
#include <cstring>

namespace engine {
namespace mongo {

MongoOptional::MongoOptional() {
    Init();
}

MongoOptional::MongoOptional(const MongoOptional& other) {
    mongoc_optional_copy(reinterpret_cast<const mongoc_optional_t*>(other.storage_),
                         reinterpret_cast<mongoc_optional_t*>(storage_));
}

MongoOptional& MongoOptional::operator=(const MongoOptional& other) {
    if (this != &other) {
        mongoc_optional_copy(reinterpret_cast<const mongoc_optional_t*>(other.storage_),
                             reinterpret_cast<mongoc_optional_t*>(storage_));
    }
    return *this;
}

void MongoOptional::Init() {
    mongoc_optional_init(reinterpret_cast<mongoc_optional_t*>(storage_));
}

bool MongoOptional::IsSet() const {
    return mongoc_optional_is_set(reinterpret_cast<const mongoc_optional_t*>(storage_));
}

bool MongoOptional::Value() const {
    return mongoc_optional_value(reinterpret_cast<const mongoc_optional_t*>(storage_));
}

void MongoOptional::SetValue(bool val) {
    mongoc_optional_set_value(reinterpret_cast<mongoc_optional_t*>(storage_), val);
}

void MongoOptional::Copy(const MongoOptional& source) {
    mongoc_optional_copy(reinterpret_cast<const mongoc_optional_t*>(source.storage_),
                         reinterpret_cast<mongoc_optional_t*>(storage_));
}

void* MongoOptional::Raw() { return storage_; }

} // namespace mongo
} // namespace engine
