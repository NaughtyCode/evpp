#include "runtime/auth/auth_backend.h"

#include <random>
#include <sstream>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace auth {

void TokenAuthBackend::AddToken(const std::string& token, const std::string& entity_id) {
	tokens_[token] = entity_id;
}

AuthResult TokenAuthBackend::Authenticate(const std::string& method,
										   const std::map<std::string, std::string>& params) {
	ENGINE_PROFILE_AUTH_AUTHENTICATE();
	AuthResult result;

	if (method != "token") {
		result.reason = "unsupported method: " + method;
		return result;
	}

	auto it = params.find("token");
	if (it == params.end()) {
		result.reason = "missing token parameter";
		return result;
	}

	auto token_it = tokens_.find(it->second);
	if (token_it == tokens_.end()) {
		result.reason = "invalid token";
		return result;
	}

	// Generate a session ID
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream ss;
	ss << std::hex << dist(gen) << dist(gen);
	std::string session_id = ss.str();

	sessions_[session_id] = token_it->second;

	result.success = true;
	result.entity_id = token_it->second;
	result.session_id = session_id;
	result.expires_at = 0;  // no expiry for token auth

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "TokenAuth: entity [{}] authenticated, session=[{}]",
					result.entity_id, session_id);

	return result;
}

bool TokenAuthBackend::ValidateSession(const std::string& session_id) {
	ENGINE_PROFILE_AUTH_VALIDATE();
	return sessions_.find(session_id) != sessions_.end();
}

void TokenAuthBackend::RevokeSession(const std::string& session_id) {
	sessions_.erase(session_id);
}

}  // namespace auth
}  // namespace engine
