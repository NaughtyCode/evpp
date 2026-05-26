#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_version.h"

#include <mongoc/mongoc.h>
#include <bson/bson.h>

namespace engine {
namespace mongo {

int MongoVersion::GetMajorVersion() { return mongoc_get_major_version(); }
int MongoVersion::GetMinorVersion() { return mongoc_get_minor_version(); }
int MongoVersion::GetMicroVersion() { return mongoc_get_micro_version(); }
const char* MongoVersion::GetVersion() { return mongoc_get_version(); }
bool MongoVersion::CheckVersion(int major, int minor, int micro) {
    return mongoc_check_version(major, minor, micro);
}

int MongoVersion::GetBsonMajorVersion() { return bson_get_major_version(); }
int MongoVersion::GetBsonMinorVersion() { return bson_get_minor_version(); }
int MongoVersion::GetBsonMicroVersion() { return bson_get_micro_version(); }
const char* MongoVersion::GetBsonVersion() { return bson_get_version(); }
bool MongoVersion::CheckBsonVersion(int major, int minor, int micro) {
    return bson_check_version(major, minor, micro);
}

} // namespace mongo
} // namespace engine

#endif
