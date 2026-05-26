#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_init.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

void MongoInit::Init() { mongoc_init(); }
void MongoInit::Cleanup() { mongoc_cleanup(); }

} // namespace mongo
} // namespace engine

#endif
