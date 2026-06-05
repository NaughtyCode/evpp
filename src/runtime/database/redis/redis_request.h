#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "runtime/database/redis/redis_result.h"

namespace engine {
namespace redis {

using RedisCompletion = std::function<void(RedisResult&&)>;

struct RedisCommandOptions {
	int timeout_ms = 0;
	std::string trace_tag;
	std::string routing_key;
};

struct RedisRequest {
	uint64_t request_id = 0;
	std::vector<std::string> argv;
	RedisCommandOptions options;
	RedisCompletion completion;
	std::optional<size_t> preferred_worker_index;
	std::chrono::steady_clock::time_point accepted_at;
	std::chrono::steady_clock::time_point deadline;
};

}  // namespace redis
}  // namespace engine
