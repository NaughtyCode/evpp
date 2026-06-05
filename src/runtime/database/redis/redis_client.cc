#include "runtime/database/redis/redis_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string_view>

#include "runtime/core/log/log.h"
#include "runtime/database/redis/redis_client_thread_group.h"

namespace engine {
namespace redis {

namespace {

std::string UpperCommand(std::string_view command) {
	std::string upper;
	upper.reserve(command.size());
	for (char ch : command) {
		upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
	}
	return upper;
}

bool IsUnsupportedCommand(const std::string& command) {
	return command == "AUTH" || command == "HELLO" || command == "SELECT" ||
		   command == "QUIT" || command == "RESET" || command == "CLIENT" ||
		   command == "MONITOR" || command == "SUBSCRIBE" ||
		   command == "PSUBSCRIBE" || command == "SSUBSCRIBE" ||
		   command == "UNSUBSCRIBE" || command == "PUNSUBSCRIBE" ||
		   command == "SUNSUBSCRIBE" || command == "MULTI" ||
		   command == "EXEC" || command == "DISCARD" ||
		   command == "WATCH" || command == "UNWATCH" ||
		   command == "WAIT" || command == "WAITAOF";
}

bool IsBlockingCommand(const std::vector<std::string>& argv) {
	if (argv.empty()) return false;
	const auto command = UpperCommand(argv.front());
	if (command == "BLPOP" || command == "BRPOP" || command == "BRPOPLPUSH" ||
		command == "BLMOVE" || command == "BLMPOP" ||
		command == "BZPOPMIN" || command == "BZPOPMAX" ||
		command == "BZMPOP") {
		return true;
	}
	if (command == "XREAD" || command == "XREADGROUP") {
		for (size_t i = 1; i < argv.size(); ++i) {
			const std::string option = UpperCommand(argv[i]);
			if (option == "STREAMS") {
				break;
			}
			if (option == "BLOCK") {
				return true;
			}
		}
	}
	return false;
}

bool IsInvalidCommandName(std::string_view command) {
	if (command.empty()) return true;
	for (unsigned char ch : command) {
		if (ch <= 0x20 || ch == 0x7f) {
			return true;
		}
	}
	return false;
}

}  // namespace

RedisClient& RedisClient::Instance() {
	static RedisClient instance;
	return instance;
}

bool RedisClient::Initialize(const RedisClientConfig& config,
							 const RedisClientStartOptions& options) {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (state_ != RedisClientState::kStopped) {
			ENGINE_LOG_WARN(GetLogger(),
							"RedisClient: initialize rejected because client state is not stopped");
			return false;
		}
		state_ = RedisClientState::kStarting;
		default_command_timeout_ms_ = config.connection.command_timeout_ms;
	}

	auto group = std::make_shared<RedisClientThreadGroup>();
	const bool ok = group->Start(config, options.wait_for_initial_connect);
	if (!ok) {
		group->Stop();
		std::lock_guard<std::mutex> lock(mutex_);
		if (state_ == RedisClientState::kStarting) {
			state_ = RedisClientState::kStopped;
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (state_ != RedisClientState::kStarting) {
			group->Stop();
			return false;
		}
		group_ = std::move(group);
		state_ = RedisClientState::kRunning;
	}
	ENGINE_LOG_INFO(GetLogger(), "RedisClient: initialized with {} worker(s)",
					config.thread.thread_count);
	return true;
}

void RedisClient::Shutdown() {
	std::shared_ptr<RedisClientThreadGroup> group;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (state_ == RedisClientState::kStopped) {
			return;
		}
		state_ = RedisClientState::kStopping;
		group = std::move(group_);
	}
	if (group) {
		group->Stop();
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		state_ = RedisClientState::kStopped;
	}
	ENGINE_LOG_INFO(GetLogger(), "RedisClient: shutdown complete");
}

bool RedisClient::IsRunning() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return state_ == RedisClientState::kRunning && group_ && group_->IsRunning();
}

bool RedisClient::IsHealthy() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return state_ == RedisClientState::kRunning && group_ && group_->IsHealthy();
}

RedisClientStats RedisClient::GetStats() const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!group_) {
		RedisClientStats stats;
		stats.state = state_;
		stats.rejected_requests = rejected_requests_.load(std::memory_order_relaxed);
		return stats;
	}
	auto stats = group_->GetStats(state_);
	stats.rejected_requests += rejected_requests_.load(std::memory_order_relaxed);
	return stats;
}

RedisSubmitResult RedisClient::Command(std::vector<std::string> argv,
									   RedisCompletion completion,
									   RedisCommandOptions options) {
	return CommandInternal(std::move(argv), std::move(completion), std::move(options),
						   std::nullopt);
}

RedisSubmitResult RedisClient::CommandInternal(
	std::vector<std::string> argv,
	RedisCompletion completion,
	RedisCommandOptions options,
	std::optional<size_t> preferred_worker_index) {
	RedisSubmitResult result;
	if (!completion) {
		result.status = RedisSubmitStatus::kInvalidArgument;
		result.error = "redis completion must not be empty";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
	if (argv.empty() || IsInvalidCommandName(argv.front())) {
		result.status = RedisSubmitStatus::kInvalidArgument;
		result.error = "redis command name is empty or contains control characters";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
	const std::string command = UpperCommand(argv.front());
	argv.front() = command;
	if (IsUnsupportedCommand(command)) {
		result.status = RedisSubmitStatus::kUnsupportedCommand;
		result.error = "redis command is not supported by RedisClient";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
	if (IsBlockingCommand(argv)) {
		result.status = RedisSubmitStatus::kUnsupportedCommand;
		result.error = "blocking redis command is not supported by RedisClient";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
	if (options.timeout_ms < 0) {
		result.status = RedisSubmitStatus::kInvalidArgument;
		result.error = "redis command timeout_ms must be >= 0";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}

	RedisRequest request;
	request.request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
	request.argv = std::move(argv);
	request.options = std::move(options);
	request.completion = std::move(completion);
	if (request.options.routing_key.empty()) {
		request.preferred_worker_index = preferred_worker_index;
	}
	int default_timeout_ms = 5000;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		default_timeout_ms = default_command_timeout_ms_;
	}
	const int timeout_ms = request.options.timeout_ms > 0
		? request.options.timeout_ms
		: default_timeout_ms;
	const auto now = std::chrono::steady_clock::now();
	request.accepted_at = now;
	request.deadline = now + std::chrono::milliseconds(timeout_ms);

	return SubmitRequest(std::move(request));
}

RedisSubmitResult RedisClient::Eval(std::string script,
									std::vector<std::string> keys,
									std::vector<std::string> args,
									RedisCompletion completion,
									RedisCommandOptions options) {
	return EvalInternal(std::move(script), std::move(keys), std::move(args),
						std::move(completion), std::move(options), std::nullopt);
}

RedisSubmitResult RedisClient::EvalInternal(
	std::string script,
	std::vector<std::string> keys,
	std::vector<std::string> args,
	RedisCompletion completion,
	RedisCommandOptions options,
	std::optional<size_t> preferred_worker_index) {
	std::vector<std::string> argv;
	argv.reserve(3 + keys.size() + args.size());
	argv.push_back("EVAL");
	argv.push_back(std::move(script));
	argv.push_back(std::to_string(keys.size()));
	for (auto& key : keys) {
		argv.push_back(std::move(key));
	}
	for (auto& arg : args) {
		argv.push_back(std::move(arg));
	}
	if (options.routing_key.empty() && !preferred_worker_index && keys.size() > 0) {
		options.routing_key = argv[3];
	}
	return CommandInternal(std::move(argv), std::move(completion), std::move(options),
						   preferred_worker_index);
}

RedisSubmitResult RedisClient::SubmitRequest(RedisRequest request) {
	std::shared_ptr<RedisClientThreadGroup> group;
	RedisClientState state;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		group = group_;
		state = state_;
	}
	RedisSubmitResult result;

	if (state == RedisClientState::kStopping) {
		result.status = RedisSubmitStatus::kShutdown;
		result.error = "redis client is shutting down";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
	if (state != RedisClientState::kRunning || !group) {
		result.status = RedisSubmitStatus::kNotRunning;
		result.error = "redis client is not running";
		rejected_requests_.fetch_add(1, std::memory_order_relaxed);
		return result;
	}

	const bool has_routing_key = !request.options.routing_key.empty();
	const std::string routing_key = request.options.routing_key;
	RedisSubmitStatus rejected = RedisSubmitStatus::kAccepted;
	std::string error;
	if (!group->Submit(request, has_routing_key, routing_key, rejected, error)) {
		result.status = rejected;
		result.error = std::move(error);
		return result;
	}

	result.status = RedisSubmitStatus::kAccepted;
	result.request_id = request.request_id;
	return result;
}

}  // namespace redis
}  // namespace engine
