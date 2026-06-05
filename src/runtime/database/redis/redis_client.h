#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/config/redis_config.h"
#include "runtime/core/engine_api.h"
#include "runtime/database/redis/redis_request.h"

namespace engine {
namespace redis {

class RedisClientThreadGroup;

enum class RedisSubmitStatus {
	kAccepted,
	kNotRunning,
	kQueueFull,
	kDisconnected,
	kInvalidCommand,
	kUnsupportedCommand,
	kShutdown
};

struct RedisSubmitResult {
	RedisSubmitStatus status = RedisSubmitStatus::kAccepted;
	uint64_t request_id = 0;
	std::string error;

	bool accepted() const { return status == RedisSubmitStatus::kAccepted; }
};

enum class RedisClientState {
	kStopped,
	kStarting,
	kRunning,
	kStopping
};

struct RedisWorkerStats {
	size_t worker_index = 0;
	bool running = false;
	bool healthy = false;
	size_t queued_requests = 0;
	size_t unsent_requests = 0;
	size_t inflight_requests = 0;
	uint64_t completed_requests = 0;
	uint64_t timed_out_requests = 0;
};

struct RedisClientStats {
	RedisClientState state = RedisClientState::kStopped;
	bool running = false;
	bool healthy = false;
	size_t worker_count = 0;
	size_t queued_requests = 0;
	size_t unsent_requests = 0;
	size_t inflight_requests = 0;
	uint64_t accepted_requests = 0;
	uint64_t rejected_requests = 0;
	uint64_t completed_requests = 0;
	uint64_t timed_out_requests = 0;
	std::vector<RedisWorkerStats> workers;
};

struct RedisClientStartOptions {
	bool required = false;
	bool wait_for_initial_connect = false;
};

class CLOUD_ENGINE_API RedisClient {
public:
	static RedisClient& Instance();

	bool Initialize(const RedisClientConfig& config,
					const RedisClientStartOptions& options = {});
	void Shutdown();

	bool IsRunning() const;
	bool IsHealthy() const;
	RedisClientStats GetStats() const;

	RedisSubmitResult Command(std::vector<std::string> argv,
							  RedisCompletion completion,
							  RedisCommandOptions options = {},
							  std::optional<size_t> preferred_worker_index = std::nullopt);
	RedisSubmitResult Eval(std::string script,
						   std::vector<std::string> keys,
						   std::vector<std::string> args,
						   RedisCompletion completion,
						   RedisCommandOptions options = {},
						   std::optional<size_t> preferred_worker_index = std::nullopt);

private:
	RedisClient() = default;

	RedisSubmitResult SubmitRequest(RedisRequest request);

	mutable std::mutex mutex_;
	RedisClientState state_ = RedisClientState::kStopped;
	std::shared_ptr<RedisClientThreadGroup> group_;
	int default_command_timeout_ms_ = 5000;
	std::atomic<uint64_t> next_request_id_{1};
};

}  // namespace redis
}  // namespace engine
