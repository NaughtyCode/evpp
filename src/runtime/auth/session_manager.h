#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <runtime/evpp/tcp_callbacks.h>

#include "runtime/auth/auth_protocol.h"
#include "runtime/core/engine_api.h"

namespace engine {
namespace auth {

class AuthBackend;

// Manages authenticated sessions — create, validate, revoke.
class ENGINE_API SessionManager {
public:
	static SessionManager& Instance();

	// Set the active auth backend.
	void SetBackend(std::unique_ptr<AuthBackend> backend);

	// Create a session for an authenticated connection.
	SessionInfo CreateSession(const std::string& entity_id, evpp::TCPConnPtr conn);

	// Validate a session (check expiry, revocation).
	bool IsSessionValid(const std::string& session_id) const;

	// Get session info by connection.
	std::optional<SessionInfo> GetSession(evpp::TCPConnPtr conn) const;

	// Get session by ID.
	std::optional<SessionInfo> GetSessionById(const std::string& session_id) const;

	// Revoke a session (logout/kick).
	void RevokeSession(const std::string& session_id);

	// Revoke by connection.
	void RevokeSession(evpp::TCPConnPtr conn);

	// Periodic cleanup of expired sessions.
	void CleanupExpired();

	// Max sessions per account.
	void SetMaxSessionsPerAccount(size_t max) { max_sessions_ = max; }

private:
	SessionManager() = default;

	std::string GenerateSessionId();

	std::unique_ptr<AuthBackend> backend_;
	mutable std::mutex mutex_;
	std::unordered_map<std::string, SessionInfo> sessions_;
	std::unordered_map<const evpp::TCPConn*, std::string> conn_to_session_;
	size_t max_sessions_ = 5;
};

}  // namespace auth
}  // namespace engine
