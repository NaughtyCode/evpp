#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "runtime/core/engine_api.h"

namespace engine {
namespace auth {

// Auth handshake protocol:
//   1. Client connects
//   2. Server sends: {type: "auth_required", methods: ["token", "jwt"]}
//   3. Client sends: {type: "auth_request", method: "token", token: "xxx"}
//   4. Server responds: {type: "auth_ok", session_id: "...", entity_id: N}
//      or: {type: "auth_failed", reason: "..."}
//   5. Normal message exchange begins

enum class AuthMethod : uint8_t {
	kToken = 0,
	kPassword = 1,
	kJwt = 2,
};

// Permission model — simple string-based permissions with levels

// Predefined permission levels. Higher levels inherit lower ones.
enum class PermissionLevel : uint8_t {
	kNone = 0,
	kRead = 1,
	kWrite = 2,
	kAdmin = 3,
	kRoot = 4,
};

using PermissionSet = std::unordered_set<std::string>;

inline const char* kPermEntityRead = "entity:read";
inline const char* kPermEntityWrite = "entity:write";
inline const char* kPermEntityDelete = "entity:delete";
inline const char* kPermEntityCreate = "entity:create";
inline const char* kPermSpaceEnter = "space:enter";
inline const char* kPermSpaceManage = "space:manage";
inline const char* kPermChatSend = "chat:send";
inline const char* kPermChatModerate = "chat:moderate";
inline const char* kPermAdmin = "admin";
inline const char* kPermServerShutdown = "server:shutdown";

// Auth result structures

struct AuthResult {
	bool success = false;
	std::string entity_id;
	std::string session_id;
	std::string reason;
	int64_t expires_at = 0;  // unix timestamp
	PermissionSet permissions;
};

struct SessionInfo {
	std::string session_id;
	std::string entity_id;
	int64_t created_at = 0;
	int64_t expires_at = 0;
	std::string auth_method;
	PermissionSet permissions;
};

}  // namespace auth
}  // namespace engine
