#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-handshake.h — customize driver handshake metadata.
class ENGINE_API MongoHandshake {
public:
    static constexpr int kAppnameMax = 128;

    static bool DataAppend(const char* driver_name, const char* driver_version, const char* platform);
};

} // namespace mongo
} // namespace engine

#endif
