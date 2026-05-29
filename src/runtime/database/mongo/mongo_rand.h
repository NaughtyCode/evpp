#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-rand.h — cryptographically secure random utilities.
class CLOUD_ENGINE_API MongoRand {
	public:
	static void Seed(const void* buf, int num);
	static void Add(const void* buf, int num, double entropy);
	static int Status();
};

}  // namespace mongo
}  // namespace engine

#endif
