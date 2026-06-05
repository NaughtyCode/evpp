#include "runtime/database/redis/redis_value.h"

#include <hiredis.h>

namespace engine {
namespace redis {

RedisValue RedisValue::Null() {
	return {};
}

RedisValue RedisValue::String(std::string value) {
	RedisValue out;
	out.type = RedisValueType::kString;
	out.string_value = std::move(value);
	return out;
}

RedisValue RedisValue::Status(std::string value) {
	RedisValue out;
	out.type = RedisValueType::kStatus;
	out.string_value = std::move(value);
	return out;
}

RedisValue RedisValue::Error(std::string value) {
	RedisValue out;
	out.type = RedisValueType::kError;
	out.string_value = std::move(value);
	return out;
}

RedisValue RedisValue::Integer(int64_t value) {
	RedisValue out;
	out.type = RedisValueType::kInteger;
	out.integer_value = value;
	return out;
}

RedisValue RedisValue::Double(double value) {
	RedisValue out;
	out.type = RedisValueType::kDouble;
	out.double_value = value;
	return out;
}

RedisValue RedisValue::Bool(bool value) {
	RedisValue out;
	out.type = RedisValueType::kBool;
	out.bool_value = value;
	return out;
}

RedisValue RedisValue::Array(std::vector<RedisValue> values) {
	RedisValue out;
	out.type = RedisValueType::kArray;
	out.array_value = std::move(values);
	return out;
}

RedisValue RedisValue::Map(std::vector<RedisValue> values) {
	RedisValue out;
	out.type = RedisValueType::kMap;
	out.array_value = std::move(values);
	return out;
}

RedisValue RedisValue::Set(std::vector<RedisValue> values) {
	RedisValue out;
	out.type = RedisValueType::kSet;
	out.array_value = std::move(values);
	return out;
}

const char* RedisValueTypeToString(RedisValueType type) {
	switch (type) {
	case RedisValueType::kNull: return "null";
	case RedisValueType::kString: return "string";
	case RedisValueType::kStatus: return "status";
	case RedisValueType::kError: return "error";
	case RedisValueType::kInteger: return "integer";
	case RedisValueType::kDouble: return "double";
	case RedisValueType::kBool: return "bool";
	case RedisValueType::kArray: return "array";
	case RedisValueType::kMap: return "map";
	case RedisValueType::kSet: return "set";
	case RedisValueType::kPush: return "push";
	case RedisValueType::kAttribute: return "attribute";
	case RedisValueType::kBigNumber: return "bignumber";
	case RedisValueType::kVerbatimString: return "verbatim_string";
	default: return "unknown";
	}
}

namespace {

std::string ReplyString(const redisReply* reply) {
	if (!reply || !reply->str || reply->len == 0) return {};
	return std::string(reply->str, reply->len);
}

std::vector<RedisValue> ReplyElements(const redisReply* reply) {
	std::vector<RedisValue> values;
	if (!reply || reply->elements == 0) return values;
	values.reserve(reply->elements);
	for (size_t i = 0; i < reply->elements; ++i) {
		values.push_back(RedisValueFromReply(reply->element[i]));
	}
	return values;
}

}  // namespace

RedisValue RedisValueFromReply(const redisReply* reply) {
	if (!reply) {
		return RedisValue::Null();
	}

	switch (reply->type) {
	case REDIS_REPLY_STRING:
		return RedisValue::String(ReplyString(reply));
	case REDIS_REPLY_STATUS:
		return RedisValue::Status(ReplyString(reply));
	case REDIS_REPLY_ERROR:
		return RedisValue::Error(ReplyString(reply));
	case REDIS_REPLY_INTEGER:
		return RedisValue::Integer(static_cast<int64_t>(reply->integer));
	case REDIS_REPLY_NIL:
		return RedisValue::Null();
	case REDIS_REPLY_ARRAY:
		return RedisValue::Array(ReplyElements(reply));
#ifdef REDIS_REPLY_DOUBLE
	case REDIS_REPLY_DOUBLE:
		return RedisValue::Double(reply->dval);
#endif
#ifdef REDIS_REPLY_BOOL
	case REDIS_REPLY_BOOL:
		return RedisValue::Bool(reply->integer != 0);
#endif
#ifdef REDIS_REPLY_MAP
	case REDIS_REPLY_MAP:
		return RedisValue::Map(ReplyElements(reply));
#endif
#ifdef REDIS_REPLY_SET
	case REDIS_REPLY_SET:
		return RedisValue::Set(ReplyElements(reply));
#endif
#ifdef REDIS_REPLY_PUSH
	case REDIS_REPLY_PUSH: {
		auto value = RedisValue::Array(ReplyElements(reply));
		value.type = RedisValueType::kPush;
		return value;
	}
#endif
#ifdef REDIS_REPLY_ATTR
	case REDIS_REPLY_ATTR: {
		auto value = RedisValue::Array(ReplyElements(reply));
		value.type = RedisValueType::kAttribute;
		return value;
	}
#endif
#ifdef REDIS_REPLY_BIGNUM
	case REDIS_REPLY_BIGNUM: {
		auto value = RedisValue::String(ReplyString(reply));
		value.type = RedisValueType::kBigNumber;
		return value;
	}
#endif
#ifdef REDIS_REPLY_VERB
	case REDIS_REPLY_VERB: {
		auto value = RedisValue::String(ReplyString(reply));
		value.type = RedisValueType::kVerbatimString;
		return value;
	}
#endif
	default:
		RedisValue value;
		value.type = RedisValueType::kUnknown;
		return value;
	}
}

}  // namespace redis
}  // namespace engine

