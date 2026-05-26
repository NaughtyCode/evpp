#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_handshake.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

bool MongoHandshake::DataAppend(const char* driver_name, const char* driver_version, const char* platform) {
    return mongoc_handshake_data_append(driver_name, driver_version, platform);
}

} // namespace mongo
} // namespace engine

#endif
