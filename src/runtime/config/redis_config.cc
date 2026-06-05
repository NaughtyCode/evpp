#include "runtime/config/redis_config.h"

#include <string>

namespace engine {

namespace {

void AppendError(RedisConfigValidationResult& result, std::string error) {
	result.valid = false;
	if (!result.errors.empty()) result.errors += "; ";
	result.errors += std::move(error);
}

void AppendWarning(RedisConfigValidationResult& result, std::string warning) {
	if (!result.warnings.empty()) result.warnings += "; ";
	result.warnings += std::move(warning);
}

}  // namespace

RedisConfigValidationResult ValidateRedisClientConfig(
	const RedisClientConfig& config) {
	RedisConfigValidationResult result;

	if (config.connection.host.empty()) {
		AppendError(result, "redis.connection.host must not be empty");
	}
	if (config.connection.port < 1 || config.connection.port > 65535) {
		AppendError(result, "redis.connection.port must be in [1, 65535]");
	}
	if (config.connection.database < 0) {
		AppendError(result, "redis.connection.database must be >= 0");
	}
	if (config.connection.connect_timeout_ms <= 0) {
		AppendError(result, "redis.connection.connect_timeout_ms must be > 0");
	}
	if (config.connection.command_timeout_ms <= 0) {
		AppendError(result, "redis.connection.command_timeout_ms must be > 0");
	}
	if (config.queue.request_queue_size == 0) {
		AppendError(result, "redis.queue.request_queue_size must be > 0");
	}
	if (config.queue.max_inflight == 0) {
		AppendError(result, "redis.queue.max_inflight must be > 0");
	}
	if (config.queue.dispatch_batch_size == 0) {
		AppendError(result, "redis.queue.dispatch_batch_size must be > 0");
	}
	if (config.thread.thread_count == 0 || config.thread.thread_count > 64) {
		AppendError(result, "redis.thread.thread_count must be in [1, 64]");
	}
	if (config.thread.main_loop_fps < 1 || config.thread.main_loop_fps > 240) {
		AppendError(result, "redis.thread.main_loop_fps must be in [1, 240]");
	}
	if (config.reconnect.initial_delay_ms <= 0) {
		AppendError(result, "redis.reconnect.initial_delay_ms must be > 0");
	}
	if (config.reconnect.max_delay_ms < config.reconnect.initial_delay_ms) {
		AppendError(result,
					"redis.reconnect.max_delay_ms must be >= initial_delay_ms");
	}
	if (config.reconnect.backoff_multiplier < 1) {
		AppendError(result,
					"redis.reconnect.backoff_multiplier must be >= 1");
	}
	if (config.log.slow_command_ms < 0) {
		AppendError(result, "redis.log.slow_command_ms must be >= 0");
	}
	if (config.reconnect.queue_while_disconnected &&
		config.connection.command_timeout_ms <= 0) {
		AppendError(result,
					"redis.reconnect.queue_while_disconnected requires command_timeout_ms > 0");
	}
	if (!config.connection.password.empty()) {
		AppendWarning(result,
					  "redis.connection.password is configured and will be redacted in logs");
	}

	return result;
}

std::string RedactedRedisPasswordForDiff(const std::string& old_password,
										 const std::string& new_password) {
	if (old_password == new_password) {
		return old_password.empty() ? "" : "<redacted:unchanged>";
	}
	return "<redacted:changed>";
}

}  // namespace engine
