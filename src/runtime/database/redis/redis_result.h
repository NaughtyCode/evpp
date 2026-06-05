#pragma once

#include <string>

#include "runtime/database/redis/redis_value.h"

namespace engine {
namespace redis {

enum class RedisResultStatus {
	kOk,
	kCommandError,
	kConnectionError,
	kAuthError,
	kProtocolError,
	kTimeout,
	kShutdown,
	kDropped
};

inline const char* RedisResultStatusToString(RedisResultStatus status) {
	switch (status) {
	case RedisResultStatus::kOk: return "ok";
	case RedisResultStatus::kCommandError: return "command_error";
	case RedisResultStatus::kConnectionError: return "connection_error";
	case RedisResultStatus::kAuthError: return "auth_error";
	case RedisResultStatus::kProtocolError: return "protocol_error";
	case RedisResultStatus::kTimeout: return "timeout";
	case RedisResultStatus::kShutdown: return "shutdown";
	case RedisResultStatus::kDropped: return "dropped";
	default: return "unknown";
	}
}

struct RedisResult {
	RedisResultStatus status = RedisResultStatus::kDropped;
	bool success = false;
	RedisValue value;
	std::string error;
	uint64_t request_id = 0;
	uint64_t elapsed_ms = 0;
};

}  // namespace redis
}  // namespace engine
