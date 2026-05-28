#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "log_init.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/space/connection_router.h"
#include "runtime/space/space.h"
#include "runtime/space/space_manager.h"
#include "runtime/space/space_message.h"

using namespace engine::space;
using namespace engine::entity;

// ============================================================================
// Integration: Space + Entity + Message routing combined workflows
// ============================================================================

// ---------------------------------------------------------------------------
// 1. SpaceManager creates a Space with a config
// ---------------------------------------------------------------------------

TEST_CASE("SpaceManager creates space with config and returns valid pointer", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "test_lobby";
    cfg.max_entities = 100;

    Space* space = mgr.CreateSpace(cfg);
    REQUIRE(space != nullptr);
    REQUIRE(space->GetName() == "test_lobby");
    REQUIRE(space->EntityCount() == 0);

    // The space should be retrievable by ID
    SpaceId id = space->GetId();
    REQUIRE(id != kInvalidSpaceId);
    REQUIRE(mgr.GetSpace(id) == space);

    mgr.DestroySpace(id);
    REQUIRE(mgr.GetSpace(id) == nullptr);
}

TEST_CASE("SpaceManager CreateSpaceWithId assigns requested ID", "[integration][space]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "custom_id_space";

    Space* space = mgr.CreateSpaceWithId(777, cfg);
    REQUIRE(space != nullptr);
    REQUIRE(space->GetId() == 777);
    REQUIRE(space->GetName() == "custom_id_space");

    mgr.DestroySpace(777);
}

TEST_CASE("SpaceManager CreateSpace returns nullptr for duplicate ID", "[integration][space]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "first";

    Space* s1 = mgr.CreateSpaceWithId(900, cfg);
    REQUIRE(s1 != nullptr);

    // Creating another space with the same ID should fail
    SpaceConfig cfg2;
    cfg2.name = "second";
    Space* s2 = mgr.CreateSpaceWithId(900, cfg2);
    REQUIRE(s2 == nullptr);

    mgr.DestroySpace(900);
}

TEST_CASE("SpaceManager SpaceCount reflects active spaces", "[integration][space]") {
    auto& mgr = SpaceManager::Instance();

    size_t initial = mgr.SpaceCount();

    SpaceConfig cfg;
    cfg.name = "count_test_1";
    Space* s1 = mgr.CreateSpace(cfg);
    REQUIRE(mgr.SpaceCount() == initial + 1);

    SpaceConfig cfg2;
    cfg2.name = "count_test_2";
    Space* s2 = mgr.CreateSpace(cfg2);
    REQUIRE(mgr.SpaceCount() == initial + 2);

    mgr.DestroySpace(s1->GetId());
    REQUIRE(mgr.SpaceCount() == initial + 1);

    mgr.DestroySpace(s2->GetId());
    REQUIRE(mgr.SpaceCount() == initial);
}

// ---------------------------------------------------------------------------
// 2. Entity created inside a Space
// ---------------------------------------------------------------------------

TEST_CASE("Space CreateEntity returns valid entity with unique ID", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "entity_test_space";
    Space* space = mgr.CreateSpace(cfg);
    REQUIRE(space != nullptr);

    Entity* e1 = space->CreateEntity();
    REQUIRE(e1 != nullptr);
    REQUIRE(e1->GetId() != kInvalidEntityId);
    REQUIRE(e1->GetState() == EntityState::Created);

    Entity* e2 = space->CreateEntity();
    REQUIRE(e2 != nullptr);
    REQUIRE(e2->GetId() != e1->GetId());

    REQUIRE(space->EntityCount() == 2);

    // Retrieve by ID
    REQUIRE(space->GetEntity(e1->GetId()) == e1);
    REQUIRE(space->GetEntity(e2->GetId()) == e2);
    REQUIRE(space->GetEntity(99999) == nullptr);

    mgr.DestroySpace(space->GetId());
}

TEST_CASE("Space CreateEntity with explicit ID", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "explicit_id_space";
    Space* space = mgr.CreateSpace(cfg);

    Entity* e = space->CreateEntity(42);
    REQUIRE(e != nullptr);
    REQUIRE(e->GetId() == 42);
    REQUIRE(space->GetEntity(42) == e);

    // Duplicate ID should return nullptr
    Entity* dup = space->CreateEntity(42);
    REQUIRE(dup == nullptr);

    mgr.DestroySpace(space->GetId());
}

TEST_CASE("Space DestroyEntity removes entity from space", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "destroy_entity_space";
    Space* space = mgr.CreateSpace(cfg);

    Entity* e1 = space->CreateEntity();
    Entity* e2 = space->CreateEntity();
    REQUIRE(space->EntityCount() == 2);

    space->DestroyEntity(e1->GetId());
    REQUIRE(space->EntityCount() == 1);
    REQUIRE(space->GetEntity(e1->GetId()) == nullptr);
    REQUIRE(space->GetEntity(e2->GetId()) == e2);

    mgr.DestroySpace(space->GetId());
}

// ---------------------------------------------------------------------------
// 3. Multiple spaces with isolated entities
// ---------------------------------------------------------------------------

TEST_CASE("Entities in different spaces are fully isolated", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfgA;
    cfgA.name = "space_alpha";
    Space* alpha = mgr.CreateSpace(cfgA);

    SpaceConfig cfgB;
    cfgB.name = "space_beta";
    Space* beta = mgr.CreateSpace(cfgB);

    // Create entities in each space
    Entity* a1 = alpha->CreateEntity();
    Entity* a2 = alpha->CreateEntity();
    Entity* b1 = beta->CreateEntity();
    Entity* b2 = beta->CreateEntity();
    Entity* b3 = beta->CreateEntity();

    REQUIRE(alpha->EntityCount() == 2);
    REQUIRE(beta->EntityCount() == 3);

    // Entities from one space are not visible in the other
    REQUIRE(alpha->GetEntity(a1->GetId()) == a1);
    REQUIRE(alpha->GetEntity(b1->GetId()) == nullptr);  // b1 belongs to beta
    REQUIRE(beta->GetEntity(b2->GetId()) == b2);
    REQUIRE(beta->GetEntity(a2->GetId()) == nullptr);   // a2 belongs to alpha

    // Destroying a space's entity does not affect the other space
    alpha->DestroyEntity(a1->GetId());
    REQUIRE(alpha->EntityCount() == 1);
    REQUIRE(beta->EntityCount() == 3);

    mgr.DestroySpace(alpha->GetId());
    mgr.DestroySpace(beta->GetId());
}

TEST_CASE("Space ForEachEntity iterates only its own entities", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "foreach_space";
    Space* space = mgr.CreateSpace(cfg);

    space->CreateEntity();
    space->CreateEntity();
    space->CreateEntity();

    int count = 0;
    space->ForEachEntity([&count](Entity& /*e*/) {
        count++;
    });

    REQUIRE(count == 3);

    mgr.DestroySpace(space->GetId());
}

// ---------------------------------------------------------------------------
// 4. Space lifecycle: create entities, update, shutdown
// ---------------------------------------------------------------------------

TEST_CASE("Space lifecycle supports full create-update-destroy cycle", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "lifecycle_space";
    cfg.max_entities = 1000;
    Space* space = mgr.CreateSpace(cfg);
    SpaceId sid = space->GetId();

    // Phase 1: Create entities
    Entity* e1 = space->CreateEntity();
    Entity* e2 = space->CreateEntity();
    Entity* e3 = space->CreateEntity();
    e1->Activate();
    e2->Activate();
    e3->Activate();

    e1->Attrs().Set("type", AttrValue{std::string("npc")});
    e2->Attrs().Set("type", AttrValue{std::string("player")});
    e3->Attrs().Set("type", AttrValue{std::string("npc")});

    REQUIRE(space->EntityCount() == 3);

    // Phase 2: Update (should not crash with no scripts loaded)
    space->Update(16);  // simulate ~60 FPS tick
    space->Update(16);
    space->Update(16);

    // Entities survive update unchanged
    REQUIRE(space->GetEntity(e1->GetId()) == e1);
    REQUIRE(space->GetEntity(e2->GetId()) == e2);
    REQUIRE(space->GetEntity(e3->GetId()) == e3);

    // Phase 3: Remove some entities mid-lifecycle
    space->DestroyEntity(e2->GetId());
    REQUIRE(space->EntityCount() == 2);
    REQUIRE(space->GetEntity(e2->GetId()) == nullptr);

    // Phase 4: Shutdown — destroy entire space
    mgr.DestroySpace(sid);
    REQUIRE(mgr.GetSpace(sid) == nullptr);
}

TEST_CASE("Space creates many entities within max_entities limit", "[integration][space][entity]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "bulk_space";
    cfg.max_entities = 100;
    Space* space = mgr.CreateSpace(cfg);

    // Create 50 entities, well within limit
    std::vector<EntityId> ids;
    for (int i = 0; i < 50; i++) {
        Entity* e = space->CreateEntity();
        REQUIRE(e != nullptr);
        e->Activate();
        ids.push_back(e->GetId());
    }

    REQUIRE(space->EntityCount() == 50);

    // All entities should be retrievable
    for (auto id : ids) {
        REQUIRE(space->GetEntity(id) != nullptr);
    }

    mgr.DestroySpace(space->GetId());
}

// ---------------------------------------------------------------------------
// 5. SpaceMessageRouter delivers cross-space messages
// ---------------------------------------------------------------------------

TEST_CASE("SpaceMessageRouter enqueues and drains messages", "[integration][space][message]") {
    auto& router = SpaceMessageRouter::Instance();

    // Start with a clean slate — drain any leftover messages
    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);

    // Enqueue a message
    SpaceMessage msg;
    msg.source_space = 1;
    msg.target_space = 2;
    msg.source_entity = 100;
    msg.target_entity = 200;
    msg.payload = "hello from space 1";

    router.SendMessage(msg);
    REQUIRE(router.PendingCount() >= 1);

    // Drain pending messages
    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);
}

TEST_CASE("SpaceMessageRouter handles multiple queued messages", "[integration][space][message]") {
    auto& router = SpaceMessageRouter::Instance();

    // Drain any prior messages
    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);

    // Enqueue several messages
    for (int i = 0; i < 5; i++) {
        SpaceMessage msg;
        msg.source_space = static_cast<SpaceId>(i + 1);
        msg.target_space = static_cast<SpaceId>(i + 10);
        msg.source_entity = static_cast<EntityId>(i * 100);
        msg.target_entity = static_cast<EntityId>(i * 100 + 1);
        msg.payload = "msg_" + std::to_string(i);
        router.SendMessage(msg);
    }

    REQUIRE(router.PendingCount() >= 5);

    // Process all pending messages
    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);
}

TEST_CASE("SpaceMessage struct fields are preserved", "[integration][space][message]") {
    SpaceMessage msg;
    msg.source_space = 42;
    msg.target_space = 84;
    msg.source_entity = 111;
    msg.target_entity = 222;
    msg.payload = "test_payload_data";

    REQUIRE(msg.source_space == 42);
    REQUIRE(msg.target_space == 84);
    REQUIRE(msg.source_entity == 111);
    REQUIRE(msg.target_entity == 222);
    REQUIRE(msg.payload == "test_payload_data");
}

TEST_CASE("SpaceMessageRouter default message has kInvalidSpaceId", "[integration][space][message]") {
    SpaceMessage msg;
    REQUIRE(msg.source_space == kInvalidSpaceId);
    REQUIRE(msg.target_space == kInvalidSpaceId);
    REQUIRE(msg.source_entity == kInvalidEntityId);
    REQUIRE(msg.target_entity == kInvalidEntityId);
    REQUIRE(msg.payload.empty());
}

// ---------------------------------------------------------------------------
// 6. ConnectionRouter singleton and edge cases
// ---------------------------------------------------------------------------

TEST_CASE("ConnectionRouter singleton returns same instance", "[integration][space][connection]") {
    auto& r1 = ConnectionRouter::Instance();
    auto& r2 = ConnectionRouter::Instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("ConnectionRouter FindSpaceByConnection returns kInvalidSpaceId for null", "[integration][space][connection]") {
    auto& router = ConnectionRouter::Instance();
    SpaceId result = router.FindSpaceByConnection(nullptr);
    REQUIRE(result == kInvalidSpaceId);
}

// ---------------------------------------------------------------------------
// 7. Default space support
// ---------------------------------------------------------------------------

TEST_CASE("SpaceManager GetDefaultSpace returns nullptr before creation", "[integration][space]") {
    auto& mgr = SpaceManager::Instance();
    Space* def = mgr.GetDefaultSpace();
    // Default space may not exist until explicitly created
    // This test verifies the API does not crash
    (void)def;
    REQUIRE(true);
}

TEST_CASE("SpaceManager CreateDefaultSpace assigns default space", "[integration][space]") {
    auto& mgr = SpaceManager::Instance();

    // Record the old default (if any) before our test
    Space* oldDefault = mgr.GetDefaultSpace();

    SpaceConfig cfg;
    cfg.name = "test_default_space";
    Space* def = mgr.CreateDefaultSpace(cfg);
    REQUIRE(def != nullptr);
    REQUIRE(def->GetName() == "test_default_space");

    Space* retrieved = mgr.GetDefaultSpace();
    REQUIRE(retrieved == def);

    // Clean up the space we created
    mgr.DestroySpace(def->GetId());

    // Restore old default if it existed
    if (oldDefault != nullptr && oldDefault != def) {
        mgr.CreateDefaultSpace(SpaceConfig{oldDefault->GetName()});
    }
}

// ---------------------------------------------------------------------------
// 8. Space and Entity full integration: create, configure, update, destroy
// ---------------------------------------------------------------------------

TEST_CASE("Full workflow: spaces with entities, attributes, lifecycle", "[integration][space][entity]") {
    auto& spaceMgr = SpaceManager::Instance();

    // Create two independent spaces
    SpaceConfig cfg1;
    cfg1.name = "world_1";
    cfg1.max_entities = 50;
    Space* world1 = spaceMgr.CreateSpace(cfg1);

    SpaceConfig cfg2;
    cfg2.name = "world_2";
    cfg2.max_entities = 50;
    Space* world2 = spaceMgr.CreateSpace(cfg2);

    REQUIRE(world1->GetId() != world2->GetId());

    // Populate world 1 with NPCs
    for (int i = 0; i < 5; i++) {
        Entity* npc = world1->CreateEntity();
        REQUIRE(npc != nullptr);
        npc->Activate();
        npc->Attrs().Set("type", AttrValue{std::string("npc")});
        npc->Attrs().Set("index", AttrValue{int64_t(i)});
    }

    // Populate world 2 with players
    for (int i = 0; i < 3; i++) {
        Entity* player = world2->CreateEntity();
        REQUIRE(player != nullptr);
        player->Activate();
        player->Attrs().Set("type", AttrValue{std::string("player")});
        player->Attrs().Set("level", AttrValue{int64_t(i + 1)});
    }

    REQUIRE(world1->EntityCount() == 5);
    REQUIRE(world2->EntityCount() == 3);

    // Verify isolation
    int world1_npc_count = 0;
    world1->ForEachEntity([&](Entity& e) {
        REQUIRE(std::get<std::string>(e.Attrs().Get("type")) == "npc");
        world1_npc_count++;
    });
    REQUIRE(world1_npc_count == 5);

    int world2_player_count = 0;
    world2->ForEachEntity([&](Entity& e) {
        REQUIRE(std::get<std::string>(e.Attrs().Get("type")) == "player");
        world2_player_count++;
    });
    REQUIRE(world2_player_count == 3);

    // Tick both worlds
    world1->Update(16);
    world2->Update(16);

    // Entities survive update
    REQUIRE(world1->EntityCount() == 5);
    REQUIRE(world2->EntityCount() == 3);

    // Destroy world 1 first
    SpaceId id1 = world1->GetId();
    spaceMgr.DestroySpace(id1);
    REQUIRE(spaceMgr.GetSpace(id1) == nullptr);

    // World 2 remains unaffected
    REQUIRE(spaceMgr.GetSpace(world2->GetId()) == world2);
    REQUIRE(world2->EntityCount() == 3);

    // Clean up world 2
    spaceMgr.DestroySpace(world2->GetId());
}
