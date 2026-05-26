#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_oid.h"

#include <bson/bson.h>

#include <cstring>

namespace engine {
namespace mongo {

MongoOid::MongoOid() {
    std::memset(bytes_, 0, 12);
}

void MongoOid::Init() {
    bson_oid_t oid;
    bson_oid_init(&oid, nullptr);
    std::memcpy(bytes_, oid.bytes, 12);
}

void MongoOid::InitFromString(const char* str) {
    bson_oid_t oid;
    bson_oid_init_from_string(&oid, str);
    std::memcpy(bytes_, oid.bytes, 12);
}

void MongoOid::InitFromData(const uint8_t* data) {
    bson_oid_t oid;
    bson_oid_init_from_data(&oid, data);
    std::memcpy(bytes_, oid.bytes, 12);
}

std::string MongoOid::ToString() const {
    char str[25];
    const auto* oid = reinterpret_cast<const bson_oid_t*>(this);
    bson_oid_to_string(oid, str);
    return std::string(str);
}

int MongoOid::Compare(const MongoOid& other) const {
    const auto* a = reinterpret_cast<const bson_oid_t*>(this);
    const auto* b = reinterpret_cast<const bson_oid_t*>(&other);
    return bson_oid_compare(a, b);
}

bool MongoOid::Equal(const MongoOid& other) const {
    const auto* a = reinterpret_cast<const bson_oid_t*>(this);
    const auto* b = reinterpret_cast<const bson_oid_t*>(&other);
    return bson_oid_equal(a, b);
}

bool MongoOid::IsValid(const char* str, size_t length) const {
    return bson_oid_is_valid(str, length);
}

uint32_t MongoOid::Hash() const {
    const auto* oid = reinterpret_cast<const bson_oid_t*>(this);
    return bson_oid_hash(oid);
}

void MongoOid::SetBytes(const uint8_t bytes[12]) {
    std::memcpy(bytes_, bytes, 12);
}

const uint8_t* MongoOid::GetBytes() const {
    return bytes_;
}

void MongoOid::Copy(const MongoOid& src) {
    const auto* s = reinterpret_cast<const bson_oid_t*>(&src);
    auto* d = reinterpret_cast<bson_oid_t*>(this);
    bson_oid_copy(s, d);
}

time_t MongoOid::GetTimeT() const {
    const auto* oid = reinterpret_cast<const bson_oid_t*>(this);
    return bson_oid_get_time_t(oid);
}

} // namespace mongo
} // namespace engine

#endif
