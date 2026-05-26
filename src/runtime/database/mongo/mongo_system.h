#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Singleton managing mongoc_init() / mongoc_cleanup() lifecycle.
// Must be initialized before any mongo operations and shut down after.
//
// Usage:
//   MongoSystem::Instance().Initialize();
//   // ... use mongo wrappers ...
//   MongoSystem::Instance().Shutdown();
class ENGINE_API MongoSystem {
public:
    static MongoSystem& Instance();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;

    MongoSystem(const MongoSystem&) = delete;
    MongoSystem& operator=(const MongoSystem&) = delete;

private:
    MongoSystem() = default;
    ~MongoSystem() = default;

    bool initialized_ = false;
};

} // namespace mongo
} // namespace engine

#endif
