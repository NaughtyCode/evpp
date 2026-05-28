#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "log_init.h"
#include "runtime/auth/auth_backend.h"
#include "runtime/auth/session_manager.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"

using namespace engine::entity;
using namespace engine::auth;

// ============================================================================
// Integration: Auth + Entity — session management for game entities
// ============================================================================

TEST_CASE("SessionManager creates session with entity ID", "[integration][auth][entity]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();
    EntityId player_id = player->GetId();

    std::string entity_id_str = std::to_string(player_id);

    // Create a session for the entity
    auto info = SessionManager::Instance().CreateSession(entity_id_str, nullptr);
    REQUIRE_FALSE(info.session_id.empty());
    REQUIRE(info.entity_id == entity_id_str);

    // Validate the session
    REQUIRE(SessionManager::Instance().IsSessionValid(info.session_id));

    mgr.DestroyAll();
}

TEST_CASE("SessionManager revoke session invalidates it", "[integration][auth][entity]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();

    std::string entity_id_str = std::to_string(player->GetId());
    auto info = SessionManager::Instance().CreateSession(entity_id_str, nullptr);
    REQUIRE_FALSE(info.session_id.empty());

    // Revoke
    SessionManager::Instance().RevokeSession(info.session_id);
    REQUIRE_FALSE(SessionManager::Instance().IsSessionValid(info.session_id));

    mgr.DestroyAll();
}

TEST_CASE("TokenAuthBackend adds and validates tokens for entities", "[integration][auth][entity]") {
    auto backend = std::make_unique<TokenAuthBackend>();
    auto* backend_ptr = backend.get();

    SessionManager::Instance().SetBackend(std::move(backend));

    // Add a token for an entity
    backend_ptr->AddToken("secret-token-123", "42");

    // Authenticate with the token
    std::map<std::string, std::string> params;
    params["token"] = "secret-token-123";
    auto result = backend_ptr->Authenticate("token", params);

    REQUIRE(result.success);
    REQUIRE(result.entity_id == "42");

    // Bad token fails
    std::map<std::string, std::string> bad_params;
    bad_params["token"] = "wrong-token";
    auto bad_result = backend_ptr->Authenticate("token", bad_params);
    REQUIRE_FALSE(bad_result.success);
}

TEST_CASE("SessionManager cleanup removes expired sessions", "[integration][auth][entity]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();

    auto info = SessionManager::Instance().CreateSession(
        std::to_string(player->GetId()), nullptr);
    REQUIRE_FALSE(info.session_id.empty());

    // Cleanup should not remove a just-created session
    SessionManager::Instance().CleanupExpired();
    REQUIRE(SessionManager::Instance().IsSessionValid(info.session_id));

    mgr.DestroyAll();
}

TEST_CASE("Multiple sessions for different entities", "[integration][auth][entity]") {
    auto& mgr = EntityManager::Instance();

    Entity* p1 = mgr.CreateEntity();
    Entity* p2 = mgr.CreateEntity();
    p1->Activate();
    p2->Activate();

    auto s1 = SessionManager::Instance().CreateSession(std::to_string(p1->GetId()), nullptr);
    auto s2 = SessionManager::Instance().CreateSession(std::to_string(p2->GetId()), nullptr);

    REQUIRE_FALSE(s1.session_id.empty());
    REQUIRE_FALSE(s2.session_id.empty());
    REQUIRE(s1.session_id != s2.session_id);
    REQUIRE(s1.entity_id != s2.entity_id);

    REQUIRE(SessionManager::Instance().IsSessionValid(s1.session_id));
    REQUIRE(SessionManager::Instance().IsSessionValid(s2.session_id));

    mgr.DestroyAll();
}
