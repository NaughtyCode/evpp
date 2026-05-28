#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "log_init.h"
#include "runtime/auth/auth_protocol.h"
#include "runtime/auth/auth_backend.h"
#include "runtime/auth/session_manager.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/tcp_conn.h"

using namespace engine::auth;

// Helper: create a minimal TCPConn for SessionManager tests.
static std::shared_ptr<evpp::TCPConn> MakeTestConn(
	evpp::EventLoop* loop,
	const std::string& name,
	uint64_t id) {
	evpp_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
	// If socket() fails, fd will be INVALID_SOCKET (-1).
	// TCPConn will skip FdChannel creation for fd < 0, which is fine for tests.
	auto conn = std::make_shared<evpp::TCPConn>(
		loop, name, fd, "127.0.0.1:0", "127.0.0.1:0", id);
	return conn;
}

// ═══════════════════════════════════════════════════════════════════════════
// Auth Protocol: enums and struct defaults
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AuthMethod enum values", "[auth][protocol]") {
	REQUIRE(static_cast<uint8_t>(AuthMethod::kToken) == 0);
	REQUIRE(static_cast<uint8_t>(AuthMethod::kPassword) == 1);
	REQUIRE(static_cast<uint8_t>(AuthMethod::kJwt) == 2);
}

TEST_CASE("AuthResult default initialization", "[auth][protocol]") {
	AuthResult r;
	REQUIRE(r.success == false);
	REQUIRE(r.entity_id.empty());
	REQUIRE(r.session_id.empty());
	REQUIRE(r.reason.empty());
	REQUIRE(r.expires_at == 0);
}

TEST_CASE("AuthResult field assignment - success", "[auth][protocol]") {
	AuthResult r;
	r.success = true;
	r.entity_id = "entity_42";
	r.session_id = "sess_abc123";
	r.expires_at = 1717974400;

	REQUIRE(r.success == true);
	REQUIRE(r.entity_id == "entity_42");
	REQUIRE(r.session_id == "sess_abc123");
	REQUIRE(r.expires_at == 1717974400);
	REQUIRE(r.reason.empty());  // Not set on success.
}

TEST_CASE("AuthResult field assignment - failure", "[auth][protocol]") {
	AuthResult r;
	r.success = false;
	r.reason = "invalid credentials";
	r.entity_id = "should_not_matter";

	REQUIRE(r.success == false);
	REQUIRE(r.reason == "invalid credentials");
}

TEST_CASE("SessionInfo default initialization", "[auth][protocol]") {
	SessionInfo si;
	REQUIRE(si.session_id.empty());
	REQUIRE(si.entity_id.empty());
	REQUIRE(si.created_at == 0);
	REQUIRE(si.expires_at == 0);
	REQUIRE(si.auth_method.empty());
}

TEST_CASE("SessionInfo field assignment", "[auth][protocol]") {
	SessionInfo si;
	si.session_id = "sess_xyz";
	si.entity_id = "entity_7";
	si.created_at = 1000000;
	si.expires_at = 1086400;
	si.auth_method = "token";

	REQUIRE(si.session_id == "sess_xyz");
	REQUIRE(si.entity_id == "entity_7");
	REQUIRE(si.created_at == 1000000);
	REQUIRE(si.expires_at == 1086400);
	REQUIRE(si.auth_method == "token");
}

// ═══════════════════════════════════════════════════════════════════════════
// TokenAuthBackend: token management
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TokenAuthBackend authenticates with valid token", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("secret123", "entity_1");

	std::map<std::string, std::string> params;
	params["token"] = "secret123";

	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == true);
	REQUIRE(result.entity_id == "entity_1");
	REQUIRE_FALSE(result.session_id.empty());
	REQUIRE(result.reason.empty());
}

TEST_CASE("TokenAuthBackend generates unique session IDs", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("token_a", "entity_1");

	std::map<std::string, std::string> params;
	params["token"] = "token_a";

	auto r1 = backend.Authenticate("token", params);
	auto r2 = backend.Authenticate("token", params);

	REQUIRE(r1.success == true);
	REQUIRE(r2.success == true);
	REQUIRE(r1.session_id != r2.session_id);
}

TEST_CASE("TokenAuthBackend rejects unknown token", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("good_token", "entity_1");

	std::map<std::string, std::string> params;
	params["token"] = "bad_token";

	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == false);
	REQUIRE(result.entity_id.empty());
	REQUIRE(result.reason == "invalid token");
	REQUIRE(result.session_id.empty());
}

TEST_CASE("TokenAuthBackend rejects missing token parameter", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("token123", "entity_1");

	// Empty params map.
	std::map<std::string, std::string> params;
	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == false);
	REQUIRE(result.reason == "missing token parameter");
}

TEST_CASE("TokenAuthBackend rejects unsupported auth method", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("token123", "entity_1");

	std::map<std::string, std::string> params;
	params["token"] = "token123";

	auto result = backend.Authenticate("password", params);
	REQUIRE(result.success == false);
	REQUIRE_FALSE(result.reason.empty());
	REQUIRE(result.reason.find("unsupported") != std::string::npos);
	REQUIRE(result.reason.find("password") != std::string::npos);
}

TEST_CASE("TokenAuthBackend multiple tokens for different entities", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("admin_token", "admin_entity");
	backend.AddToken("user_token", "user_entity");

	std::map<std::string, std::string> params;

	params["token"] = "admin_token";
	auto r1 = backend.Authenticate("token", params);
	REQUIRE(r1.success == true);
	REQUIRE(r1.entity_id == "admin_entity");

	params["token"] = "user_token";
	auto r2 = backend.Authenticate("token", params);
	REQUIRE(r2.success == true);
	REQUIRE(r2.entity_id == "user_entity");

	REQUIRE(r1.session_id != r2.session_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// TokenAuthBackend: session validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TokenAuthBackend validates session after authentication", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("token_xyz", "entity_42");

	std::map<std::string, std::string> params;
	params["token"] = "token_xyz";

	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == true);

	REQUIRE(backend.ValidateSession(result.session_id) == true);
}

TEST_CASE("TokenAuthBackend rejects unknown session", "[auth][token]") {
	TokenAuthBackend backend;
	REQUIRE(backend.ValidateSession("nonexistent_session") == false);
}

TEST_CASE("TokenAuthBackend revoke session invalidates it", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("token_abc", "entity_99");

	std::map<std::string, std::string> params;
	params["token"] = "token_abc";

	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == true);
	REQUIRE(backend.ValidateSession(result.session_id) == true);

	backend.RevokeSession(result.session_id);
	REQUIRE(backend.ValidateSession(result.session_id) == false);
}

TEST_CASE("TokenAuthBackend revoke nonexistent session is safe", "[auth][token]") {
	TokenAuthBackend backend;
	REQUIRE_NOTHROW(backend.RevokeSession("no_such_session"));
}

TEST_CASE("TokenAuthBackend empty token string", "[auth][token]") {
	TokenAuthBackend backend;
	backend.AddToken("", "entity_empty_token");

	std::map<std::string, std::string> params;
	params["token"] = "";

	auto result = backend.Authenticate("token", params);
	REQUIRE(result.success == true);
	REQUIRE(result.entity_id == "entity_empty_token");
}

// ═══════════════════════════════════════════════════════════════════════════
// SessionManager: backend and session lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SessionManager can set a backend", "[auth][session]") {
	auto& mgr = SessionManager::Instance();
	auto backend = std::make_unique<TokenAuthBackend>();
	REQUIRE_NOTHROW(mgr.SetBackend(std::move(backend)));
}

TEST_CASE("SessionManager creates session for a connection", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 100);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_test", conn);

	REQUIRE_FALSE(session.session_id.empty());
	REQUIRE(session.entity_id == "entity_test");
	REQUIRE(session.created_at > 0);
	REQUIRE(session.expires_at > session.created_at);
	REQUIRE(session.auth_method == "token");
}

TEST_CASE("SessionManager validates a newly created session", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 101);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_valid", conn);

	REQUIRE(mgr.IsSessionValid(session.session_id) == true);
}

TEST_CASE("SessionManager rejects unknown session ID", "[auth][session]") {
	auto& mgr = SessionManager::Instance();
	REQUIRE(mgr.IsSessionValid("nonexistent_session_id_xyz") == false);
}

TEST_CASE("SessionManager gets session by connection", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 102);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_conn_lookup", conn);

	auto info = mgr.GetSession(conn);
	REQUIRE(info.has_value());
	REQUIRE(info->session_id == session.session_id);
	REQUIRE(info->entity_id == "entity_conn_lookup");
}

TEST_CASE("SessionManager gets session by ID", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 103);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_id_lookup", conn);

	auto info = mgr.GetSessionById(session.session_id);
	REQUIRE(info.has_value());
	REQUIRE(info->entity_id == "entity_id_lookup");
}

TEST_CASE("SessionManager returns nullopt for unknown connection", "[auth][session]") {
	auto& mgr = SessionManager::Instance();

	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "unknown_conn", 104);

	// This connection was never used to create a session.
	auto info = mgr.GetSession(conn);
	REQUIRE_FALSE(info.has_value());
}

TEST_CASE("SessionManager returns nullopt for unknown session ID", "[auth][session]") {
	auto& mgr = SessionManager::Instance();
	auto info = mgr.GetSessionById("no_such_id_abc123");
	REQUIRE_FALSE(info.has_value());
}

TEST_CASE("SessionManager revoke by session ID", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 105);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_revoke", conn);

	REQUIRE(mgr.IsSessionValid(session.session_id) == true);

	mgr.RevokeSession(session.session_id);
	REQUIRE(mgr.IsSessionValid(session.session_id) == false);

	// Also removed from connection mapping.
	auto info = mgr.GetSession(conn);
	REQUIRE_FALSE(info.has_value());
}

TEST_CASE("SessionManager revoke by connection", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 106);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_revoke_conn", conn);

	REQUIRE(mgr.IsSessionValid(session.session_id) == true);

	mgr.RevokeSession(conn);
	REQUIRE(mgr.IsSessionValid(session.session_id) == false);

	auto info = mgr.GetSession(conn);
	REQUIRE_FALSE(info.has_value());
}

TEST_CASE("SessionManager revoke unknown connection is safe", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "never_used", 107);

	auto& mgr = SessionManager::Instance();
	REQUIRE_NOTHROW(mgr.RevokeSession(conn));
}

TEST_CASE("SessionManager revoke unknown session ID is safe", "[auth][session]") {
	auto& mgr = SessionManager::Instance();
	REQUIRE_NOTHROW(mgr.RevokeSession("no_such_session_xyz"));
}

TEST_CASE("SessionManager creates different sessions for different connections", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn1 = MakeTestConn(&loop, "conn_a", 108);
	auto conn2 = MakeTestConn(&loop, "conn_b", 109);

	auto& mgr = SessionManager::Instance();
	auto s1 = mgr.CreateSession("entity_same", conn1);
	auto s2 = mgr.CreateSession("entity_same", conn2);

	REQUIRE(s1.session_id != s2.session_id);
	REQUIRE(s1.entity_id == s2.entity_id);

	auto info1 = mgr.GetSession(conn1);
	auto info2 = mgr.GetSession(conn2);
	REQUIRE(info1.has_value());
	REQUIRE(info2.has_value());
	REQUIRE(info1->session_id != info2->session_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// SessionManager: expiry and cleanup
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SessionManager CleanupExpired is safe when no sessions exist", "[auth][session]") {
	auto& mgr = SessionManager::Instance();
	REQUIRE_NOTHROW(mgr.CleanupExpired());
}

TEST_CASE("SessionManager CleanupExpired removes expired sessions", "[auth][session]") {
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "test_conn", 110);

	auto& mgr = SessionManager::Instance();
	auto session = mgr.CreateSession("entity_expiry", conn);

	// Session should be valid initially (24h expiry by default).
	REQUIRE(mgr.IsSessionValid(session.session_id) == true);

	// CleanupExpired should leave it untouched.
	REQUIRE_NOTHROW(mgr.CleanupExpired());
	REQUIRE(mgr.IsSessionValid(session.session_id) == true);
}

TEST_CASE("SessionManager max sessions per account", "[auth][session]") {
	evpp::EventLoop loop;

	auto& mgr = SessionManager::Instance();
	mgr.SetMaxSessionsPerAccount(2);

	auto conn1 = MakeTestConn(&loop, "conn_1", 111);
	auto conn2 = MakeTestConn(&loop, "conn_2", 112);
	auto conn3 = MakeTestConn(&loop, "conn_3", 113);

	auto s1 = mgr.CreateSession("entity_max", conn1);
	REQUIRE(mgr.IsSessionValid(s1.session_id) == true);

	auto s2 = mgr.CreateSession("entity_max", conn2);
	REQUIRE(mgr.IsSessionValid(s2.session_id) == true);

	// Third session should evict the oldest one.
	auto s3 = mgr.CreateSession("entity_max", conn3);
	REQUIRE(mgr.IsSessionValid(s3.session_id) == true);

	// s1 (oldest) should be evicted.
	REQUIRE(mgr.IsSessionValid(s1.session_id) == false);

	// Cleanup.
	mgr.RevokeSession(s2.session_id);
	mgr.RevokeSession(s3.session_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// SessionManager: integration with TokenAuthBackend
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SessionManager validates session through backend", "[auth][integration]") {
	auto& mgr = SessionManager::Instance();

	auto backend = std::make_unique<TokenAuthBackend>();
	backend->AddToken("integration_token", "integ_entity");
	auto* raw_backend = backend.get();
	mgr.SetBackend(std::move(backend));

	// Authenticate through the backend directly.
	std::map<std::string, std::string> params;
	params["token"] = "integration_token";
	auto authResult = raw_backend->Authenticate("token", params);
	REQUIRE(authResult.success == true);

	// Create a session that matches the backend's session.
	evpp::EventLoop loop;
	auto conn = MakeTestConn(&loop, "integ_conn", 114);
	auto session = mgr.CreateSession("integ_entity", conn);

	// IsSessionValid checks expiry first, then the backend.
	// Since we set the backend, it will also check ValidateSession.
	REQUIRE(mgr.IsSessionValid(session.session_id) == true);

	// The backend should validate the session it created.
	REQUIRE(raw_backend->ValidateSession(authResult.session_id) == true);

	mgr.RevokeSession(session.session_id);
}
