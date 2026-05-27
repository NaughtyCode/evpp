#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"

namespace engine {
namespace auth {

// Auth handshake protocol:
//   1. Client connects
//   2. Server sends: {type: "auth_required", methods: ["token"]}
//   3. Client sends: {type: "auth_request", method: "token", token: "xxx"}
//   4. Server responds: {type: "auth_ok", session_id: "...", entity_id: N}
//      or: {type: "auth_failed", reason: "..."}
//   5. Normal message exchange begins

enum class AuthMethod : uint8_t {
	kToken = 0,
	kPassword = 1,
	kJwt = 2,
};

struct AuthResult {
	bool success = false;
	std::string entity_id;
	std::string session_id;
	std::string reason;
	int64_t expires_at = 0;  // unix timestamp
};

struct SessionInfo {
	std::string session_id;
	std::string entity_id;
	int64_t created_at = 0;
	int64_t expires_at = 0;
	std::string auth_method;
};

}  // namespace auth
}  // namespace engine
