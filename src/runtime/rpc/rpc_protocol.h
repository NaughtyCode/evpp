#pragma once

#include <cstdint>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine {
namespace rpc {

// RPC wire format (msgpack-encoded):
//   [uint32: total_length] [msgpack_map: header] [msgpack: body]
//
// Header map:
//   { msgid: uint32, service: string, method: string, type: "request"|"response"|"error" }
//
// Response types:
//   "response" → body is the return value
//   "error"    → body is { code: int, message: string }

enum class RpcMessageType : uint8_t {
	kRequest = 0,
	kResponse = 1,
	kError = 2,
};

struct RpcHeader {
	uint32_t msgid = 0;
	std::string service;
	std::string method;
	RpcMessageType type = RpcMessageType::kRequest;
};

struct RpcRequest {
	RpcHeader header;
	std::string body;  // msgpack-encoded arguments
};

struct RpcResponse {
	uint32_t msgid = 0;
	bool success = true;
	int error_code = 0;
	std::string error_message;
	std::string body;  // msgpack-encoded result
};

}  // namespace rpc
}  // namespace engine
