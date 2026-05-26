#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_rand.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

void MongoRand::Seed(const void* buf, int num) {
    mongoc_rand_seed(buf, num);
}

void MongoRand::Add(const void* buf, int num, double entropy) {
    mongoc_rand_add(buf, num, entropy);
}

int MongoRand::Status() {
    return mongoc_rand_status();
}

} // namespace mongo
} // namespace engine

#endif
