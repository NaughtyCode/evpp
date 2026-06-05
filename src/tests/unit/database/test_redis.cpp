#include <atomic>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <hiredis.h>

#include "runtime/database/redis/bind/redis_bind.h"
#include "runtime/database/redis/redis_client.h"
#include "runtime/database/redis/redis_value.h"
#include "runtime/vm/async_result_dispatcher.h"
#include "runtime/vm/vm.h"

namespace {

using engine::redis::RedisClient;
using engine::redis::RedisCommandOptions;
using engine::redis::RedisResult;
using engine::redis::RedisSubmitStatus;
using engine::redis::RedisValueFromReply;
using engine::redis::RedisValueType;
using engine::RedisClientConfig;

}  // namespace

TEST_CASE("RedisClient rejects unsupported and invalid commands before enqueue",
		  "[redis][client]") {
	RedisClient::Instance().Shutdown();

	bool completion_called = false;
	auto unsupported = RedisClient::Instance().Command(
		{"AUTH", "secret"},
		[&](RedisResult&&) { completion_called = true; });
	REQUIRE_FALSE(unsupported.accepted());
	REQUIRE(unsupported.status == RedisSubmitStatus::kUnsupportedCommand);
	REQUIRE(unsupported.request_id == 0);
	REQUIRE_FALSE(completion_called);

	auto blocking = RedisClient::Instance().Command(
		{"XREAD", "BLOCK", "1000", "STREAMS", "s", "0"},
		[&](RedisResult&&) { completion_called = true; });
	REQUIRE_FALSE(blocking.accepted());
	REQUIRE(blocking.status == RedisSubmitStatus::kUnsupportedCommand);
	REQUIRE(blocking.request_id == 0);
	REQUIRE_FALSE(completion_called);

	RedisCommandOptions options;
	options.timeout_ms = -1;
	auto invalid_timeout = RedisClient::Instance().Command(
		{"PING"},
		[&](RedisResult&&) { completion_called = true; },
		options);
	REQUIRE_FALSE(invalid_timeout.accepted());
	REQUIRE(invalid_timeout.status == RedisSubmitStatus::kInvalidArgument);
	REQUIRE(invalid_timeout.request_id == 0);
	REQUIRE_FALSE(completion_called);
}

TEST_CASE("RedisClient reports not running without allocating public request ids",
		  "[redis][client]") {
	RedisClient::Instance().Shutdown();

	bool completion_called = false;
	auto result = RedisClient::Instance().Command(
		{"PING"},
		[&](RedisResult&&) { completion_called = true; });
	REQUIRE_FALSE(result.accepted());
	REQUIRE(result.status == RedisSubmitStatus::kNotRunning);
	REQUIRE(result.request_id == 0);
	REQUIRE_FALSE(completion_called);

	auto xread_key_named_block = RedisClient::Instance().Command(
		{"XREAD", "STREAMS", "BLOCK", "0"},
		[&](RedisResult&&) { completion_called = true; });
	REQUIRE_FALSE(xread_key_named_block.accepted());
	REQUIRE(xread_key_named_block.status == RedisSubmitStatus::kNotRunning);
	REQUIRE(xread_key_named_block.request_id == 0);
	REQUIRE_FALSE(completion_called);
}

TEST_CASE("RedisClient validates direct initialize config", "[redis][client]") {
	RedisClient::Instance().Shutdown();

	RedisClientConfig config;
	config.thread.thread_count = 0;

	REQUIRE_FALSE(RedisClient::Instance().Initialize(config));
	REQUIRE_FALSE(RedisClient::Instance().IsRunning());
}

TEST_CASE("RedisValue converts hiredis replies including binary strings",
		  "[redis][value]") {
	char binary[] = {'a', '\0', 'b'};
	redisReply string_reply{};
	string_reply.type = REDIS_REPLY_STRING;
	string_reply.str = binary;
	string_reply.len = sizeof(binary);

	auto string_value = RedisValueFromReply(&string_reply);
	REQUIRE(string_value.type == RedisValueType::kString);
	REQUIRE(string_value.string_value.size() == 3);
	REQUIRE(string_value.string_value[1] == '\0');

	redisReply integer_reply{};
	integer_reply.type = REDIS_REPLY_INTEGER;
	integer_reply.integer = 42;

	redisReply nil_reply{};
	nil_reply.type = REDIS_REPLY_NIL;

	redisReply* elements[] = {&integer_reply, &nil_reply};
	redisReply array_reply{};
	array_reply.type = REDIS_REPLY_ARRAY;
	array_reply.elements = 2;
	array_reply.element = elements;

	auto array_value = RedisValueFromReply(&array_reply);
	REQUIRE(array_value.type == RedisValueType::kArray);
	REQUIRE(array_value.array_value.size() == 2);
	REQUIRE(array_value.array_value[0].type == RedisValueType::kInteger);
	REQUIRE(array_value.array_value[0].integer_value == 42);
	REQUIRE(array_value.array_value[1].type == RedisValueType::kNull);
}

TEST_CASE("AsyncResultDispatcher enqueues across threads and dispatches on owner",
		  "[redis][dispatcher]") {
	engine::AsyncResultDispatcher dispatcher;
	std::atomic<bool> enqueue_result{false};
	int value = 0;

	std::thread producer([&] {
		enqueue_result.store(
			dispatcher.Enqueue([&] { value = 42; }),
			std::memory_order_release);
	});
	producer.join();

	REQUIRE(enqueue_result.load(std::memory_order_acquire));
	REQUIRE(value == 0);
	REQUIRE(dispatcher.Dispatch(1) == 1);
	REQUIRE(value == 42);

	dispatcher.ShutdownOnOwnerThread();
	REQUIRE_FALSE(dispatcher.Enqueue([] {}));
}

TEST_CASE("Redis Lua command closures keep binding context alive",
		  "[redis][lua]") {
	RedisClient::Instance().Shutdown();

	engine::ScriptVM vm;
	engine::script::ExportRedis(vm);

	std::string output;
	REQUIRE(vm.DoString(R"lua(
local command = redis.command
redis = nil
package.loaded.redis = nil
collectgarbage("collect")
local ok, err = command({"AUTH", "secret"}, function(_) end)
return tostring(ok) .. "|" .. type(err) .. "|" .. tostring(redis == nil)
)lua",
						 "redis_context_lifetime",
						 nullptr,
						 &output));
	REQUIRE(output == "false|string|true");
}
