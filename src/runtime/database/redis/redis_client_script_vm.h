#pragma once

#define ENGINE_REDIS_INTERNAL
#include "runtime/database/redis/module_access.h"
#undef ENGINE_REDIS_INTERNAL

#include <cstddef>

#include "runtime/vm/vm.h"

namespace engine {
namespace redis {

class RedisClientThread;
class RedisClient;

enum RedisClientScriptVMSlots {
	kRedisPtrThread = 1,
	kRedisPtrScriptVM = 2,
	kRedisPtrClient = 3,
	kRedisPtrDispatcher = 4
};

class RedisClientScriptVM : public ScriptVM {
public:
	RedisClientScriptVM();
	~RedisClientScriptVM() override;

	void RegisterSubsystemObjects(RedisClientThread* thread, size_t worker_index);

	RedisClientThread* GetRedisClientThread() const;
	RedisClient* GetRedisClient() const;
	size_t worker_index() const { return worker_index_; }

private:
	size_t worker_index_ = 0;
};

}  // namespace redis
}  // namespace engine

