#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "runtime/core/engine_api.h"

struct redisReply;

namespace engine {
namespace redis {

enum class RedisValueType {
	kNull,
	kString,
	kStatus,
	kError,
	kInteger,
	kDouble,
	kBool,
	kArray,
	kMap,
	kSet,
	kPush,
	kAttribute,
	kBigNumber,
	kVerbatimString,
	kUnknown
};

struct RedisValue {
	RedisValueType type = RedisValueType::kNull;
	std::string string_value;
	int64_t integer_value = 0;
	double double_value = 0.0;
	bool bool_value = false;
	std::vector<RedisValue> array_value;

	static RedisValue Null();
	static RedisValue String(std::string value);
	static RedisValue Status(std::string value);
	static RedisValue Error(std::string value);
	static RedisValue Integer(int64_t value);
	static RedisValue Double(double value);
	static RedisValue Bool(bool value);
	static RedisValue Array(std::vector<RedisValue> values);
	static RedisValue Map(std::vector<RedisValue> values);
	static RedisValue Set(std::vector<RedisValue> values);
};

CLOUD_ENGINE_API const char* RedisValueTypeToString(RedisValueType type);
CLOUD_ENGINE_API RedisValue RedisValueFromReply(const redisReply* reply);

}  // namespace redis
}  // namespace engine

