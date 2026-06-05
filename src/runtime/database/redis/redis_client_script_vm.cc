#include "runtime/database/redis/redis_client_script_vm.h"

#include "runtime/database/redis/redis_client.h"

namespace engine {
namespace redis {

/* Creates a Redis-aware script VM wrapper for a worker thread. */
RedisClientScriptVM::RedisClientScriptVM() = default;
/* Destroys the Redis-aware script VM wrapper after script shutdown. */
RedisClientScriptVM::~RedisClientScriptVM() = default;

/* Registers Redis-specific native pointers in the worker-owned script VM. */
void RedisClientScriptVM::RegisterSubsystemObjects(RedisClientThread* thread,
												   size_t worker_index) {
	worker_index_ = worker_index;
	ReserveCustomPtrSlots(kRedisPtrDispatcher);
	SetCustomPtr(kRedisPtrThread, thread);
	SetCustomPtr(kRedisPtrScriptVM, this);
	SetCustomPtr(kRedisPtrClient, &RedisClient::Instance());
	SetCustomPtr(kRedisPtrDispatcher, GetAsyncDispatcher().get());
}

/* Returns the Redis worker thread associated with this script VM. */
RedisClientThread* RedisClientScriptVM::GetRedisClientThread() const {
	return GetCustomPtrAs<RedisClientThread>(kRedisPtrThread);
}

/* Returns the process-wide Redis client exposed to Redis scripts. */
RedisClient* RedisClientScriptVM::GetRedisClient() const {
	return GetCustomPtrAs<RedisClient>(kRedisPtrClient);
}

}  // namespace redis
}  // namespace engine
