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

}  // namespace auth
}  // namespace engine
