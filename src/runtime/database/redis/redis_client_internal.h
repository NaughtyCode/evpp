#pragma once

#define ENGINE_REDIS_INTERNAL
#include "runtime/database/redis/module_access.h"
#undef ENGINE_REDIS_INTERNAL

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "runtime/database/redis/redis_client.h"

namespace engine {
namespace redis {
namespace internal {

class RedisClientAccess {
public:
	static RedisSubmitResult Command(std::vector<std::string> argv,
									 RedisCompletion completion,
									 RedisCommandOptions options,
									 std::optional<size_t> preferred_worker_index) {
		return RedisClient::Instance().CommandInternal(
			std::move(argv),
			std::move(completion),
			std::move(options),
			preferred_worker_index);
	}

	static RedisSubmitResult Eval(std::string script,
								  std::vector<std::string> keys,
								  std::vector<std::string> args,
								  RedisCompletion completion,
								  RedisCommandOptions options,
								  std::optional<size_t> preferred_worker_index) {
		return RedisClient::Instance().EvalInternal(
			std::move(script),
			std::move(keys),
			std::move(args),
			std::move(completion),
			std::move(options),
			preferred_worker_index);
	}
};

}  // namespace internal
}  // namespace redis
}  // namespace engine
