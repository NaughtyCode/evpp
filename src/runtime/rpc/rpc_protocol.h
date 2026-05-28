#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "runtime/core/engine_api.h"

namespace engine {
namespace rpc {

// RPC wire format:
//   [uint32: total_length] [header JSON/msgpack] [body JSON/msgpack]
//
// Header map (serialized form):
//   { msgid: uint32, service: string, method: string, type: "request"|"response"|"error" }
//
// All body fields carry opaque payload strings; encoding is chosen by the
// transport layer (typically JSON via Lua bindings).

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
	std::string body;

	RpcRequest() = default;
	RpcRequest(RpcRequest&&) = default;
	RpcRequest& operator=(RpcRequest&&) = default;
	RpcRequest(const RpcRequest&) = default;
	RpcRequest& operator=(const RpcRequest&) = default;
};

struct RpcResponse {
	uint32_t msgid = 0;
	bool success = true;
	int error_code = 0;
	std::string error_message;
	std::string body;

	RpcResponse() = default;
	RpcResponse(RpcResponse&&) = default;
	RpcResponse& operator=(RpcResponse&&) = default;
	RpcResponse(const RpcResponse&) = default;
	RpcResponse& operator=(const RpcResponse&) = default;

	// Create an error response without allocating error_message when unused.
	static RpcResponse Error(uint32_t msgid, int code, std::string message) {
		RpcResponse r;
		r.msgid = msgid;
		r.success = false;
		r.error_code = code;
		r.error_message = std::move(message);
		return r;
	}

	static RpcResponse Ok(uint32_t msgid, std::string body = {}) {
		RpcResponse r;
		r.msgid = msgid;
		r.success = true;
		r.body = std::move(body);
		return r;
	}
};

}  // namespace rpc
}  // namespace engine
