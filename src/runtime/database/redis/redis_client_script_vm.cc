#include "runtime/database/redis/redis_client_script_vm.h"

#include "runtime/database/redis/redis_client.h"

namespace engine {
namespace redis {

RedisClientScriptVM::RedisClientScriptVM() = default;
RedisClientScriptVM::~RedisClientScriptVM() = default;

void RedisClientScriptVM::RegisterSubsystemObjects(RedisClientThread* thread,
												   size_t worker_index) {
	worker_index_ = worker_index;
	ReserveCustomPtrSlots(kRedisPtrDispatcher);
	SetCustomPtr(kRedisPtrThread, thread);
	SetCustomPtr(kRedisPtrScriptVM, this);
	SetCustomPtr(kRedisPtrClient, &RedisClient::Instance());
	SetCustomPtr(kRedisPtrDispatcher, GetAsyncDispatcher().get());
}

RedisClientThread* RedisClientScriptVM::GetRedisClientThread() const {
	return GetCustomPtrAs<RedisClientThread>(kRedisPtrThread);
}

RedisClient* RedisClientScriptVM::GetRedisClient() const {
	return GetCustomPtrAs<RedisClient>(kRedisPtrClient);
}

}  // namespace redis
}  // namespace engine
