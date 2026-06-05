#pragma once

#define ENGINE_REDIS_INTERNAL
#include "runtime/database/redis/module_access.h"
#undef ENGINE_REDIS_INTERNAL

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "runtime/config/redis_config.h"
#include "runtime/database/redis/redis_client_thread.h"

namespace engine {
namespace redis {

class RedisClientThreadGroup {
public:
	RedisClientThreadGroup();
	~RedisClientThreadGroup();

	RedisClientThreadGroup(const RedisClientThreadGroup&) = delete;
	RedisClientThreadGroup& operator=(const RedisClientThreadGroup&) = delete;

	bool Start(const RedisClientConfig& config, bool wait_for_initial_connect);
	void Stop();

	bool Submit(RedisRequest& request,
				bool has_routing_key,
				const std::string& routing_key,
				RedisSubmitStatus& rejected_status,
				std::string& error);

	bool IsRunning() const;
	bool IsHealthy() const;
	RedisClientStats GetStats(RedisClientState state) const;

private:
	size_t ChooseWorker(bool has_routing_key,
						const std::string& routing_key,
						RedisSubmitStatus& rejected_status,
						std::string& error);
	bool ReserveUnsentSlot();
	void ReleaseUnsentSlot();

	RedisClientConfig config_;
	std::shared_ptr<RedisSharedCounters> counters_;
	std::vector<std::unique_ptr<RedisClientThread>> workers_;
	std::atomic<size_t> round_robin_{0};
	mutable std::mutex mutex_;
	bool stopping_ = false;
};

}  // namespace redis
}  // namespace engine
