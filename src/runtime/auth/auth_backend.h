#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "runtime/auth/auth_protocol.h"
#include "runtime/core/engine_api.h"

namespace engine {
namespace auth {

// Abstract auth backend interface.
class ENGINE_API AuthBackend {
public:
	virtual ~AuthBackend() = default;

	// Authenticate a connection using the given method and parameters.
	virtual AuthResult Authenticate(const std::string& method,
	                                 const std::map<std::string, std::string>& params) = 0;

	// Validate an existing session.
	virtual bool ValidateSession(const std::string& session_id) = 0;

	// Revoke a session.
	virtual void RevokeSession(const std::string& session_id) = 0;

	// Check if an entity has a specific permission.
	virtual bool HasPermission(const std::string& entity_id,
	                            const std::string& permission);

	// Grant a permission to an entity.
	virtual void GrantPermission(const std::string& entity_id,
	                              const std::string& permission);

	// Revoke a permission from an entity.
	virtual void RevokePermission(const std::string& entity_id,
	                               const std::string& permission);

protected:
	std::unordered_map<std::string, PermissionSet> entity_permissions_;
};

// Static token-based auth (dev/testing).

class ENGINE_API TokenAuthBackend : public AuthBackend {
public:
	void AddToken(const std::string& token, const std::string& entity_id);

	AuthResult Authenticate(const std::string& method,
	                         const std::map<std::string, std::string>& params) override;
	bool ValidateSession(const std::string& session_id) override;
	void RevokeSession(const std::string& session_id) override;

private:
	std::unordered_map<std::string, std::string> tokens_;      // token → entity_id
	std::unordered_map<std::string, std::string> sessions_;    // session_id → entity_id
};

// JWT (JSON Web Token) auth backend.
//
// Supports HS256 (HMAC-SHA256) tokens. The backend is configured with a
// pre-shared secret. On authentication, it verifies the JWT signature,
// checks expiry, and extracts the entity_id from the "sub" claim.

class ENGINE_API JwtAuthBackend : public AuthBackend {
public:
	// Set the shared secret for HMAC-SHA256 verification.
	void SetSecret(const std::string& secret);

	AuthResult Authenticate(const std::string& method,
	                         const std::map<std::string, std::string>& params) override;
	bool ValidateSession(const std::string& session_id) override;
	void RevokeSession(const std::string& session_id) override;

private:
	// Verify a JWT token. Returns the payload JSON string on success, empty on failure.
	std::string VerifyToken(const std::string& token);

	// Base64url decode a string.
	static std::string Base64UrlDecode(std::string_view input);

	std::string secret_;
	std::unordered_map<std::string, SessionInfo> sessions_;
};

}  // namespace auth
}  // namespace engine
