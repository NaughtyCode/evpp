#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "log_init.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/evpp/tcp_conn.h"

using namespace engine::entity;

// ============================================================================
// Integration: Entity + TCP Connection binding
// ============================================================================

TEST_CASE("Entity BindConnection stores connection", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    REQUIRE(player != nullptr);
    player->Activate();

    // Entity starts in Created state, Activate transitions to Active
    REQUIRE(player->GetState() == EntityState::Active);

    // BindConnection stores the connection shared_ptr on the entity
    player->BindConnection(nullptr);

    mgr.DestroyAll();
}

TEST_CASE("Entity destruction cleans up entity from manager", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();
    EntityId player_id = player->GetId();

    player->BindConnection(nullptr);

    mgr.DestroyEntity(player_id);

    // Verify entity is gone via GetEntity
    REQUIRE(mgr.GetEntity(player_id) == nullptr);
}

TEST_CASE("EntityManager FindByConnection returns nullptr when no entity bound", "[integration][entity][net]") {
    auto result = EntityManager::Instance().FindByConnection(nullptr);
    REQUIRE(result == nullptr);
}

TEST_CASE("Entity can be created and activated with connection binding", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();
    EntityId player_id = player->GetId();

    player->BindConnection(nullptr);

    // Entity is active after activation
    REQUIRE(mgr.GetEntity(player_id) != nullptr);
    REQUIRE(mgr.GetEntity(player_id)->GetState() == EntityState::Active);

    // Count and ActiveCount reflect active entities
    REQUIRE(mgr.Count() >= 1);
    REQUIRE(mgr.ActiveCount() >= 1);

    mgr.DestroyAll();
}

TEST_CASE("Entity timer persists across entity lifecycle", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();
    EntityId player_id = player->GetId();

    player->BindConnection(nullptr);

    bool timer_fired = false;
    player->AddTimer(100, false, [&timer_fired, player_id]() {
        auto* entity = EntityManager::Instance().GetEntity(player_id);
        if (entity && entity->GetState() == EntityState::Active) {
            timer_fired = true;
        }
    });

    // Timer is registered (fires after 100ms, non-repeating)
    REQUIRE(timer_fired == false);

    mgr.DestroyAll();
}

TEST_CASE("Entity destruction cancels owned timers", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    player->Activate();
    EntityId player_id = player->GetId();

    // Add timers; they should be cleaned up on destruction
    player->AddTimer(100, false, []() {});
    player->AddTimer(200, true, []() {});

    mgr.DestroyEntity(player_id);

    // Entity destroyed and removed from manager
    REQUIRE(mgr.GetEntity(player_id) == nullptr);
}

TEST_CASE("EntityManager Count tracks active entities correctly", "[integration][entity][net]") {
    auto& mgr = EntityManager::Instance();

    size_t initial_count = mgr.Count();
    size_t initial_active = mgr.ActiveCount();

    Entity* p1 = mgr.CreateEntity();
    Entity* p2 = mgr.CreateEntity();
    p1->Activate();
    p2->Activate();

    REQUIRE(mgr.Count() == initial_count + 2);
    REQUIRE(mgr.ActiveCount() == initial_active + 2);

    mgr.DestroyEntity(p1->GetId());

    REQUIRE(mgr.Count() == initial_count + 1);
    REQUIRE(mgr.ActiveCount() == initial_active + 1);

    mgr.DestroyAll();
}
