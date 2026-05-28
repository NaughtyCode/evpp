#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "log_init.h"
#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/entity/attribute.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"

using namespace engine::entity;
using namespace engine::aoi;

// ============================================================================
// Integration: EntityManager + AOIManager combined workflows
// ============================================================================

// ---------------------------------------------------------------------------
// 1. Entity creation via EntityManager, then register with AOI spatial grid
// ---------------------------------------------------------------------------

TEST_CASE("EntityManager entity registered in AOI is queryable by position", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    Entity* player = mgr.CreateEntity();
    REQUIRE(player != nullptr);
    player->Activate();

    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(player->GetId(), 50.0f);
    aoi.OnEntityMove(player->GetId(), 250.0f, 300.0f);

    // Query should find the player at its position
    auto nearby = aoi.QueryRadius(250.0f, 300.0f, 10.0f);
    REQUIRE(nearby.size() == 1);
    REQUIRE(nearby[0] == player->GetId());

    // Query far away should return empty
    auto far_away = aoi.QueryRadius(900.0f, 900.0f, 10.0f);
    REQUIRE(far_away.empty());

    aoi.UnregisterEntity(player->GetId());
    mgr.DestroyAll();
}

// ---------------------------------------------------------------------------
// 2. Entity attributes set/get combined with AOI spatial tracking
// ---------------------------------------------------------------------------

TEST_CASE("Entity attributes persist across AOI registration lifecycle", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    Entity* e = mgr.CreateEntity();
    REQUIRE(e != nullptr);
    e->Activate();

    // Set entity attributes before AOI registration
    e->Attrs().Set("name", AttrValue{std::string("guardian")});
    e->Attrs().Set("hp", AttrValue{int64_t(500)});
    e->Attrs().Set("speed", AttrValue{3.5});

    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(e->GetId(), 60.0f);
    aoi.OnEntityMove(e->GetId(), 150.0f, 200.0f);

    // Attributes must survive AOI operations unchanged
    REQUIRE(std::get<std::string>(e->Attrs().Get("name")) == "guardian");
    REQUIRE(std::get<int64_t>(e->Attrs().Get("hp")) == 500);
    REQUIRE(std::get<double>(e->Attrs().Get("speed")) == 3.5);
    REQUIRE(e->Attrs().Count() == 3);

    // Update an attribute after AOI operations
    e->Attrs().Set("hp", AttrValue{int64_t(480)});
    REQUIRE(std::get<int64_t>(e->Attrs().Get("hp")) == 480);

    aoi.UnregisterEntity(e->GetId());
    mgr.DestroyAll();
}

// ---------------------------------------------------------------------------
// 3. AOI SpatialGrid: insert entity at a position, query nearby by spatial range
// ---------------------------------------------------------------------------

TEST_CASE("SpatialGrid insert and range query with multiple positions", "[integration][entity][aoi]") {
    auto grid = std::make_unique<SpatialGrid>(2000.0f, 2000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    // Insert three entities at distinct positions
    aoi.RegisterEntity(10, 30.0f);
    aoi.RegisterEntity(20, 30.0f);
    aoi.RegisterEntity(30, 30.0f);

    aoi.OnEntityMove(10, 100.0f, 100.0f);   // cluster A
    aoi.OnEntityMove(20, 130.0f, 120.0f);   // cluster A (near 10)
    aoi.OnEntityMove(30, 1500.0f, 1500.0f); // cluster B (far away)

    // Query cluster A
    auto resultA = aoi.QueryRadius(100.0f, 100.0f, 50.0f);
    REQUIRE(resultA.size() >= 1);

    // Query cluster B
    auto resultB = aoi.QueryRadius(1500.0f, 1500.0f, 50.0f);
    REQUIRE(resultB.size() == 1);
    REQUIRE(resultB[0] == 30);

    // Empty region
    auto empty = aoi.QueryRadius(900.0f, 100.0f, 10.0f);
    REQUIRE(empty.empty());

    aoi.UnregisterEntity(10);
    aoi.UnregisterEntity(20);
    aoi.UnregisterEntity(30);
}

// ---------------------------------------------------------------------------
// 4. Multiple entities in grid with different positions
// ---------------------------------------------------------------------------

TEST_CASE("Multiple entities at different positions yield correct AOI visibility", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    // Create entities through EntityManager, activate them
    Entity* eA = mgr.CreateEntity();
    Entity* eB = mgr.CreateEntity();
    Entity* eC = mgr.CreateEntity();
    Entity* eD = mgr.CreateEntity();
    eA->Activate();
    eB->Activate();
    eC->Activate();
    eD->Activate();

    EntityId idA = eA->GetId();
    EntityId idB = eB->GetId();
    EntityId idC = eC->GetId();
    EntityId idD = eD->GetId();

    auto grid = std::make_unique<SpatialGrid>(2000.0f, 2000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    // All entities have the same large AOI radius
    aoi.RegisterEntity(idA, 100.0f);
    aoi.RegisterEntity(idB, 100.0f);
    aoi.RegisterEntity(idC, 100.0f);
    aoi.RegisterEntity(idD, 100.0f);

    // Place A and B close together, C and D far apart
    aoi.OnEntityMove(idA, 200.0f, 200.0f);
    aoi.OnEntityMove(idB, 220.0f, 210.0f);
    aoi.OnEntityMove(idC, 800.0f, 800.0f);
    aoi.OnEntityMove(idD, 1600.0f, 1600.0f);

    // A should see B (within 100-unit radius)
    auto visA = aoi.GetVisibleEntities(idA);
    bool aSeesB = false;
    for (auto id : visA) { if (id == idB) aSeesB = true; }
    REQUIRE(aSeesB);

    // C should not see D (too far apart)
    auto visC = aoi.GetVisibleEntities(idC);
    bool cSeesD = false;
    for (auto id : visC) { if (id == idD) cSeesD = true; }
    REQUIRE_FALSE(cSeesD);

    // D should not see C either
    auto visD = aoi.GetVisibleEntities(idD);
    bool dSeesC = false;
    for (auto id : visD) { if (id == idC) dSeesC = true; }
    REQUIRE_FALSE(dSeesC);

    aoi.UnregisterEntity(idA);
    aoi.UnregisterEntity(idB);
    aoi.UnregisterEntity(idC);
    aoi.UnregisterEntity(idD);
    mgr.DestroyAll();
}

// ---------------------------------------------------------------------------
// 5. Remove entity from AOI after spatial operations
// ---------------------------------------------------------------------------

TEST_CASE("Remove entity from AOI makes it invisible to range queries", "[integration][entity][aoi]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(100, 80.0f);
    aoi.RegisterEntity(200, 80.0f);

    aoi.OnEntityMove(100, 400.0f, 400.0f);
    aoi.OnEntityMove(200, 420.0f, 410.0f);

    // Both should be visible to each other
    auto vis100 = aoi.GetVisibleEntities(100);
    REQUIRE(vis100.size() == 1);
    REQUIRE(vis100[0] == 200);

    // Remove entity 200 from AOI
    aoi.UnregisterEntity(200);
    REQUIRE(aoi.EntityCount() == 1);

    // Entity 100 should no longer see 200
    auto visAfter = aoi.GetVisibleEntities(100);
    REQUIRE(visAfter.empty());

    // QueryRadius should not find 200 either
    auto range = aoi.QueryRadius(400.0f, 400.0f, 50.0f);
    bool found200 = false;
    for (auto id : range) { if (id == 200) found200 = true; }
    REQUIRE_FALSE(found200);

    // Removing again is safe
    REQUIRE_NOTHROW(aoi.UnregisterEntity(200));

    aoi.UnregisterEntity(100);
}

// ---------------------------------------------------------------------------
// 6. AOI visibility events fire when entities enter/leave range
// ---------------------------------------------------------------------------

TEST_CASE("AOI visibility enter event fires on entity approach", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    Entity* observer = mgr.CreateEntity();
    Entity* target = mgr.CreateEntity();
    observer->Activate();
    target->Activate();

    EntityId obsId = observer->GetId();
    EntityId tgtId = target->GetId();

    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(obsId, 60.0f);
    aoi.RegisterEntity(tgtId, 60.0f);

    int enter_count = 0;
    int leave_count = 0;

    aoi.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        if (entered) enter_count++;
        else leave_count++;
    });

    // Observer is placed first
    aoi.OnEntityMove(obsId, 200.0f, 200.0f);
    REQUIRE(enter_count == 0);

    // Target moves into observer's range — should trigger enter event
    aoi.OnEntityMove(tgtId, 230.0f, 220.0f);
    REQUIRE(enter_count >= 1);

    aoi.UnregisterEntity(obsId);
    aoi.UnregisterEntity(tgtId);
    mgr.DestroyAll();
}

TEST_CASE("AOI visibility leave event fires on entity departure", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    Entity* e1 = mgr.CreateEntity();
    Entity* e2 = mgr.CreateEntity();
    e1->Activate();
    e2->Activate();

    EntityId id1 = e1->GetId();
    EntityId id2 = e2->GetId();

    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(id1, 60.0f);
    aoi.RegisterEntity(id2, 60.0f);

    int enter_count = 0;
    int leave_count = 0;

    aoi.SetEventCallback([&](EntityId /*observer*/, EntityId /*target*/, bool entered) {
        if (entered) enter_count++;
        else leave_count++;
    });

    // Both entities start close together — enter event fires
    aoi.OnEntityMove(id1, 500.0f, 500.0f);
    aoi.OnEntityMove(id2, 520.0f, 510.0f);
    REQUIRE(enter_count >= 1);

    // Move e2 far away — leave event should fire
    aoi.OnEntityMove(id2, 900.0f, 900.0f);
    REQUIRE(leave_count >= 1);

    aoi.UnregisterEntity(id1);
    aoi.UnregisterEntity(id2);
    mgr.DestroyAll();
}

TEST_CASE("AOI events are fired symmetrically when AOI radii overlap", "[integration][entity][aoi]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(1, 60.0f);
    aoi.RegisterEntity(2, 60.0f);

    int events_for_1 = 0;
    int events_for_2 = 0;

    aoi.SetEventCallback([&](EntityId observer, EntityId /*target*/, bool /*entered*/) {
        if (observer == 1) events_for_1++;
        else if (observer == 2) events_for_2++;
    });

    // Place 1 first, then 2 nearby
    aoi.OnEntityMove(1, 300.0f, 300.0f);
    aoi.OnEntityMove(2, 310.0f, 310.0f);

    // Both should receive events when visibility is established
    // (at least one each, since visibility is bidirectional with equal radii)
    REQUIRE(events_for_1 >= 1);
    REQUIRE(events_for_2 >= 1);

    aoi.UnregisterEntity(1);
    aoi.UnregisterEntity(2);
}

// ---------------------------------------------------------------------------
// 7. Edge cases: no-op operations, empty grids, unregistered entities
// ---------------------------------------------------------------------------

TEST_CASE("AOI operations on empty grid are safe", "[integration][entity][aoi]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    REQUIRE(aoi.EntityCount() == 0);

    auto result = aoi.QueryRadius(500.0f, 500.0f, 200.0f);
    REQUIRE(result.empty());

    auto vis = aoi.GetVisibleEntities(404);
    REQUIRE(vis.empty());

    REQUIRE_NOTHROW(aoi.OnEntityMove(999, 100.0f, 100.0f));
    REQUIRE_NOTHROW(aoi.UnregisterEntity(999));
}

TEST_CASE("EntityManager DestroyEntity does not affect unrelated entities in AOI", "[integration][entity][aoi]") {
    auto& mgr = EntityManager::Instance();

    Entity* e1 = mgr.CreateEntity();
    Entity* e2 = mgr.CreateEntity();
    Entity* e3 = mgr.CreateEntity();
    e1->Activate();
    e2->Activate();
    e3->Activate();

    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager aoi(std::move(grid));

    aoi.RegisterEntity(e1->GetId(), 50.0f);
    aoi.RegisterEntity(e2->GetId(), 50.0f);
    aoi.RegisterEntity(e3->GetId(), 50.0f);

    aoi.OnEntityMove(e1->GetId(), 100.0f, 100.0f);
    aoi.OnEntityMove(e2->GetId(), 120.0f, 120.0f);
    aoi.OnEntityMove(e3->GetId(), 130.0f, 110.0f);

    REQUIRE(aoi.EntityCount() == 3);

    // Destroy e2 in EntityManager
    mgr.DestroyEntity(e2->GetId());
    REQUIRE(mgr.GetEntity(e2->GetId()) == nullptr);

    // e1 and e3 should still exist in EntityManager
    REQUIRE(mgr.GetEntity(e1->GetId()) != nullptr);
    REQUIRE(mgr.GetEntity(e3->GetId()) != nullptr);

    // AOI still tracks e2 until explicitly unregistered (grid state is separate)
    // Unregister and clean up the rest
    aoi.UnregisterEntity(e1->GetId());
    aoi.UnregisterEntity(e2->GetId());
    aoi.UnregisterEntity(e3->GetId());
    mgr.DestroyAll();
}
