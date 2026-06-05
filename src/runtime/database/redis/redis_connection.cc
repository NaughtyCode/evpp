#include "runtime/database/redis/redis_connection.h"

namespace engine {
namespace redis {

const char* RedisConnectionErrorString(int status, const char* errstr) {
	if (status == 0) return "ok";
	return errstr && errstr[0] != '\0' ? errstr : "redis connection error";
}

}  // namespace redis
}  // namespace engine

