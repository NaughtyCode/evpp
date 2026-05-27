#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-init.h — global driver lifecycle.
class ENGINE_API MongoInit {
	public:
	static void Init();
	static void Cleanup();
};

}  // namespace mongo
}  // namespace engine

#endif
