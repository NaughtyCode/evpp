#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "log_init.h"
#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/entity/entity_id.h"

using namespace engine::aoi;
using engine::entity::EntityId;

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — construction
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid construction with valid parameters", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    // 1000/100 = 10x10 grid
    REQUIRE(grid.Size() == 0);
}

TEST_CASE("SpatialGrid construction with small cell size", "[aoi][spatial_grid]") {
    SpatialGrid grid(100.0f, 100.0f, 10.0f);
    // 100/10 = 10x10 grid
    REQUIRE(grid.Size() == 0);
}

TEST_CASE("SpatialGrid construction with cell size larger than world", "[aoi][spatial_grid]") {
    SpatialGrid grid(100.0f, 100.0f, 200.0f);
    // ceil(100/200) = 1, minimum 1x1 grid
    REQUIRE(grid.Size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — insert
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::Insert adds entity and increases Size", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    REQUIRE(grid.Size() == 0);

    grid.Insert(1, 150.0f, 250.0f);
    REQUIRE(grid.Size() == 1);

    grid.Insert(2, 550.0f, 750.0f);
    REQUIRE(grid.Size() == 2);
}

TEST_CASE("SpatialGrid::Insert at world boundaries", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    // Insert at origin
    grid.Insert(1, 0.0f, 0.0f);
    REQUIRE(grid.Size() == 1);

    // Insert at far corner
    grid.Insert(2, 999.0f, 999.0f);
    REQUIRE(grid.Size() == 2);

    // Insert outside world bounds (should clamp)
    grid.Insert(3, 1500.0f, 1500.0f);
    REQUIRE(grid.Size() == 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — remove
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::Remove removes entity and decreases Size", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 100.0f, 200.0f);
    grid.Insert(2, 500.0f, 500.0f);
    REQUIRE(grid.Size() == 2);

    grid.Remove(1);
    REQUIRE(grid.Size() == 1);

    grid.Remove(2);
    REQUIRE(grid.Size() == 0);
}

TEST_CASE("SpatialGrid::Remove for non-existent entity is safe", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    REQUIRE_NOTHROW(grid.Remove(999));
    REQUIRE(grid.Size() == 0);
}

TEST_CASE("SpatialGrid::Remove is idempotent", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(10, 300.0f, 300.0f);
    REQUIRE(grid.Size() == 1);

    grid.Remove(10);
    REQUIRE(grid.Size() == 0);

    // Removing again should be safe
    REQUIRE_NOTHROW(grid.Remove(10));
    REQUIRE(grid.Size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — update
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::Update moves entity to new position", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 50.0f, 50.0f);    // cell (0,0)
    REQUIRE(grid.Size() == 1);

    grid.Update(1, 550.0f, 550.0f);  // cell (5,5)
    REQUIRE(grid.Size() == 1);
}

TEST_CASE("SpatialGrid::Update on non-existent entity inserts it", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Update(42, 200.0f, 300.0f);
    REQUIRE(grid.Size() == 1);

    // A second update keeps size at 1 (updates position)
    grid.Update(42, 400.0f, 500.0f);
    REQUIRE(grid.Size() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — query radius
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::QueryRadius finds entity within range", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 200.0f, 200.0f);
    grid.Insert(2, 900.0f, 900.0f);

    auto result = grid.QueryRadius(200.0f, 200.0f, 50.0f);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 1);
}

TEST_CASE("SpatialGrid::QueryRadius returns empty for no matches", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 200.0f, 200.0f);
    grid.Insert(2, 500.0f, 500.0f);

    auto result = grid.QueryRadius(900.0f, 900.0f, 10.0f);
    REQUIRE(result.empty());
}

TEST_CASE("SpatialGrid::QueryRadius finds multiple entities in range", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 100.0f, 100.0f);
    grid.Insert(2, 120.0f, 120.0f);
    grid.Insert(3, 110.0f, 90.0f);
    grid.Insert(4, 800.0f, 800.0f);  // far away

    auto result = grid.QueryRadius(100.0f, 100.0f, 30.0f);
    // Should find entities 1, 2, 3 within 30 units of (100,100)
    REQUIRE(result.size() == 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — query AOI
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::QueryAOI returns entities in same and neighboring cells", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 150.0f, 150.0f);  // cell (1,1)
    grid.Insert(2, 180.0f, 180.0f);  // cell (1,1) same cell
    grid.Insert(3, 250.0f, 150.0f);  // cell (2,1) neighboring cell
    grid.Insert(4, 850.0f, 850.0f);  // cell (8,8) far away

    auto result = grid.QueryAOI(1);
    // Should find entities 1, 2 (same cell) and 3 (neighboring cell)
    // Entity 4 should not be in result
    REQUIRE(result.size() == 3);
}

TEST_CASE("SpatialGrid::QueryAOIAt returns entities by position", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 50.0f, 50.0f);
    grid.Insert(2, 80.0f, 80.0f);
    grid.Insert(3, 950.0f, 50.0f);  // far away, different cell

    auto result = grid.QueryAOIAt(50.0f, 50.0f);
    // Entities 1 and 2 are in same/neighboring cells of (50,50)
    REQUIRE(result.size() == 2);
}

TEST_CASE("SpatialGrid::QueryAOI on empty grid returns empty", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    auto result = grid.QueryAOIAt(500.0f, 500.0f);
    REQUIRE(result.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// SpatialGrid — clear
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpatialGrid::Clear removes all entities", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 100.0f, 100.0f);
    grid.Insert(2, 200.0f, 200.0f);
    grid.Insert(3, 300.0f, 300.0f);
    REQUIRE(grid.Size() == 3);

    grid.Clear();
    REQUIRE(grid.Size() == 0);

    // Grid is reusable after clear
    grid.Insert(4, 400.0f, 400.0f);
    REQUIRE(grid.Size() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// AOIManager — entity registration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AOIManager registers entities and tracks count", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    REQUIRE(mgr.EntityCount() == 0);

    mgr.RegisterEntity(1, 50.0f);
    REQUIRE(mgr.EntityCount() == 1);

    mgr.RegisterEntity(2, 100.0f);
    REQUIRE(mgr.EntityCount() == 2);
}

TEST_CASE("AOIManager::UnregisterEntity decreases count", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 100.0f);
    REQUIRE(mgr.EntityCount() == 2);

    mgr.UnregisterEntity(1);
    REQUIRE(mgr.EntityCount() == 1);

    mgr.UnregisterEntity(2);
    REQUIRE(mgr.EntityCount() == 0);
}

TEST_CASE("AOIManager::UnregisterEntity for non-registered entity is safe", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    REQUIRE_NOTHROW(mgr.UnregisterEntity(999));
    REQUIRE(mgr.EntityCount() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// AOIManager — visibility
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AOIManager::GetVisibleEntities returns empty before first move", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    auto visible = mgr.GetVisibleEntities(1);
    REQUIRE(visible.empty());
}

TEST_CASE("AOIManager entities become visible after moving into range", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 50.0f);

    // Entity 1 moves into the grid first -- no one else is present yet.
    mgr.OnEntityMove(1, 100.0f, 100.0f);

    // Entity 2 moves near entity 1 -- entity 2's visibility is recomputed
    // and it discovers entity 1. Entity 1's visibility set is NOT updated
    // because RecomputeVisibility only fires for the entity that moved.
    mgr.OnEntityMove(2, 110.0f, 110.0f);

    // Entity 2 sees entity 1 because entity 2 was the last to move.
    auto visible2 = mgr.GetVisibleEntities(2);
    REQUIRE(visible2.size() == 1);
    REQUIRE(visible2[0] == 1);
}

TEST_CASE("AOIManager entities out of range are not visible", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 20.0f);   // small AOI radius
    mgr.RegisterEntity(2, 20.0f);

    // Move entity 2 first so it exists in the grid, then entity 1 so its
    // visibility is recomputed against entity 2's position.
    mgr.OnEntityMove(2, 800.0f, 800.0f);  // far away
    mgr.OnEntityMove(1, 100.0f, 100.0f);

    auto visible = mgr.GetVisibleEntities(1);
    REQUIRE(visible.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// AOIManager — radius queries
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AOIManager::QueryRadius finds entities at a point", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 100.0f);
    mgr.RegisterEntity(2, 100.0f);

    mgr.OnEntityMove(1, 100.0f, 100.0f);
    mgr.OnEntityMove(2, 500.0f, 500.0f);

    auto result = mgr.QueryRadius(100.0f, 100.0f, 30.0f);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 1);

    auto result2 = mgr.QueryRadius(500.0f, 500.0f, 30.0f);
    REQUIRE(result2.size() == 1);
    REQUIRE(result2[0] == 2);
}

TEST_CASE("AOIManager::QueryRadius returns empty for uncovered areas", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.OnEntityMove(1, 100.0f, 100.0f);

    // Query far away from the entity
    auto result = mgr.QueryRadius(900.0f, 900.0f, 10.0f);
    REQUIRE(result.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// AOIManager — enter/leave events
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AOIManager fires enter event when entities come into range", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 50.0f);

    int enter_count = 0;
    int leave_count = 0;
    EntityId last_observer = 0;
    EntityId last_target = 0;

    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        if (entered) {
            enter_count++;
        } else {
            leave_count++;
        }
        last_observer = observer;
        last_target = target;
    });

    // Move entity 1 first, so it exists in the grid
    mgr.OnEntityMove(1, 100.0f, 100.0f);
    // Entity 2 moves into entity 1's AOI range
    mgr.OnEntityMove(2, 110.0f, 110.0f);

    REQUIRE(enter_count >= 1);
    // The observer should be entity 1 (already in grid), seeing entity 2 enter
    // or entity 2 seeing entity 1 enter (bidirectional visibility)
    REQUIRE(enter_count >= 1);
}

TEST_CASE("AOIManager fires leave event when entities move out of range", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 50.0f);

    int enter_count = 0;
    int leave_count = 0;

    mgr.SetEventCallback([&](EntityId /*observer*/, EntityId /*target*/, bool entered) {
        if (entered) {
            enter_count++;
        } else {
            leave_count++;
        }
    });

    // Bring entities together
    mgr.OnEntityMove(1, 100.0f, 100.0f);
    mgr.OnEntityMove(2, 110.0f, 110.0f);

    int enter_after_approach = enter_count;

    // Move entity 2 far away
    mgr.OnEntityMove(2, 900.0f, 900.0f);

    REQUIRE(leave_count >= 1);
}

TEST_CASE("AOIManager does not fire events when no callback is set", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 50.0f);

    // No callback set — moves should not crash
    REQUIRE_NOTHROW(mgr.OnEntityMove(1, 100.0f, 100.0f));
    REQUIRE_NOTHROW(mgr.OnEntityMove(2, 110.0f, 110.0f));
    REQUIRE_NOTHROW(mgr.OnEntityMove(2, 900.0f, 900.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// AOIManager — edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AOIManager::OnEntityMove before RegisterEntity is ignored", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    // Moving an unregistered entity should not crash
    REQUIRE_NOTHROW(mgr.OnEntityMove(99, 500.0f, 500.0f));
    auto visible = mgr.GetVisibleEntities(99);
    REQUIRE(visible.empty());
}

TEST_CASE("AOIManager visibility becomes symmetric after both entities move", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 50.0f);
    mgr.RegisterEntity(2, 50.0f);

    // Step 1: Entity 1 moves first -- no one else in grid
    mgr.OnEntityMove(1, 200.0f, 200.0f);

    // Step 2: Entity 2 moves near entity 1 -- entity 2's visibility is computed,
    // entity 2 now sees entity 1, but entity 1's visibility is stale.
    mgr.OnEntityMove(2, 210.0f, 210.0f);

    // Step 3: Move entity 1 again so its visibility is recomputed --
    // now entity 1 sees entity 2 as well.
    mgr.OnEntityMove(1, 200.0f, 200.0f);

    auto visible1 = mgr.GetVisibleEntities(1);
    auto visible2 = mgr.GetVisibleEntities(2);

    bool one_sees_two = false;
    bool two_sees_one = false;
    for (auto id : visible1) { if (id == 2) one_sees_two = true; }
    for (auto id : visible2) { if (id == 1) two_sees_one = true; }

    REQUIRE(one_sees_two == true);
    REQUIRE(two_sees_one == true);
}

TEST_CASE("AOIManager::GetVisibleEntities for unknown entity returns empty", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    auto visible = mgr.GetVisibleEntities(404);
    REQUIRE(visible.empty());
}
