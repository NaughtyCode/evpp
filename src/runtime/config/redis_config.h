#pragma once

#include <cstddef>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine {

struct RedisConnectionConfig {
	std::string host = "127.0.0.1";
	int port = 6379;
	std::string username;
	std::string password;
	int database = 0;
	int connect_timeout_ms = 5000;
	int command_timeout_ms = 5000;
	bool keepalive = true;
};

struct RedisQueueConfig {
	size_t request_queue_size = 4096;
	size_t max_inflight = 4096;
	size_t dispatch_batch_size = 256;
};

struct RedisThreadConfig {
	size_t thread_count = 4;
	int main_loop_fps = 60;
};

struct RedisScriptConfig {
	std::string redis_scripts_dir = "resources/script/redis";
	bool auto_load = true;
};

struct RedisLogConfig {
	bool enabled = true;
	int slow_command_ms = 100;
};

struct RedisReconnectConfig {
	bool enabled = true;
	int initial_delay_ms = 500;
	int max_delay_ms = 5000;
	int backoff_multiplier = 2;
	bool queue_while_disconnected = false;
};

struct RedisClientConfig {
	RedisConnectionConfig connection;
	RedisQueueConfig queue;
	RedisThreadConfig thread;
	RedisScriptConfig script;
	RedisLogConfig log;
	RedisReconnectConfig reconnect;
};

struct RedisConfigValidationResult {
	bool valid = true;
	std::string errors;
	std::string warnings;
};

CLOUD_ENGINE_API RedisConfigValidationResult ValidateRedisClientConfig(
	const RedisClientConfig& config);

CLOUD_ENGINE_API std::string RedactedRedisPasswordForDiff(
	const std::string& old_password,
	const std::string& new_password);

}  // namespace engine
