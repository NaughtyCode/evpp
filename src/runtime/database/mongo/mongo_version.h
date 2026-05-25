#pragma once

#include <cstdint>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-version-functions.h and bson-version-functions.h.
class ENGINE_API MongoVersion {
public:
    // mongoc version
    static int GetMajorVersion();
    static int GetMinorVersion();
    static int GetMicroVersion();
    static const char* GetVersion();
    static bool CheckVersion(int required_major, int required_minor, int required_micro);

    // bson version
    static int GetBsonMajorVersion();
    static int GetBsonMinorVersion();
    static int GetBsonMicroVersion();
    static const char* GetBsonVersion();
    static bool CheckBsonVersion(int required_major, int required_minor, int required_micro);
};

} // namespace mongo
} // namespace engine
