#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_system.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

MongoSystem& MongoSystem::Instance() {
    static MongoSystem instance;
    return instance;
}

bool MongoSystem::Initialize() {
    if (initialized_) return true;
    mongoc_init();
    initialized_ = true;
    return true;
}

void MongoSystem::Shutdown() {
    if (!initialized_) return;
    mongoc_cleanup();
    initialized_ = false;
}

bool MongoSystem::IsInitialized() const {
    return initialized_;
}

} // namespace mongo
} // namespace engine

#endif
