#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "log_init.h"
#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/entity/entity_id.h"
#include "runtime/aoi/bind/aoi_bind.h"
#include "runtime/vm/vm.h"

using namespace engine::aoi;
using engine::entity::EntityId;

namespace {

bool Contains(const std::vector<EntityId>& values, EntityId id) {
	return std::find(values.begin(), values.end(), id) != values.end();
}

struct EventRecord {
	EntityId observer = 0;
	EntityId target = 0;
	bool entered = false;
};

bool HasEvent(const std::vector<EventRecord>& events,
			  EntityId observer,
			  EntityId target,
			  bool entered) {
	return std::find_if(events.begin(), events.end(), [&](const EventRecord& event) {
		return event.observer == observer && event.target == target && event.entered == entered;
	}) != events.end();
}

struct AOIBindFixture {
	engine::ScriptVM vm;

	AOIBindFixture() {
		engine::script::ExportAOI(vm);
	}

	bool RunLua(const std::string& code, std::string* err = nullptr) {
		return vm.DoString(code, "test_aoi_bind", err);
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_aoi_bind", nullptr, &result);
	}
};

}  // namespace

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

TEST_CASE("SpatialGrid rejects invalid dimensions", "[aoi][spatial_grid]") {
    REQUIRE_THROWS(SpatialGrid(0.0f, 100.0f, 10.0f));
    REQUIRE_THROWS(SpatialGrid(100.0f, -1.0f, 10.0f));
    REQUIRE_THROWS(SpatialGrid(100.0f, 100.0f, 0.0f));
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

TEST_CASE("SpatialGrid::Insert updates duplicate entity instead of duplicating", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 100.0f, 100.0f);
    grid.Insert(1, 900.0f, 900.0f);

    REQUIRE(grid.Size() == 1);
    REQUIRE(grid.QueryRadius(100.0f, 100.0f, 1.0f).empty());

    auto result = grid.QueryRadius(900.0f, 900.0f, 1.0f);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 1);
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

TEST_CASE("SpatialGrid keeps moved cell references valid after remove", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 10.0f, 10.0f);
    grid.Insert(2, 20.0f, 20.0f);
    grid.Insert(3, 30.0f, 30.0f);

    grid.Remove(2);
    grid.Update(3, 500.0f, 500.0f);

    auto origin = grid.QueryRadius(0.0f, 0.0f, 100.0f);
    REQUIRE(Contains(origin, 1));
    REQUIRE_FALSE(Contains(origin, 2));
    REQUIRE_FALSE(Contains(origin, 3));

    auto moved = grid.QueryRadius(500.0f, 500.0f, 1.0f);
    REQUIRE(moved.size() == 1);
    REQUIRE(moved[0] == 3);
    REQUIRE(grid.Size() == 2);
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

TEST_CASE("SpatialGrid::QueryRadius supports entities outside world bounds", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 1500.0f, 1500.0f);

    auto result = grid.QueryRadius(1500.0f, 1500.0f, 1.0f);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 1);
}

TEST_CASE("SpatialGrid::QueryRadius rejects invalid radius safely", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    grid.Insert(1, 100.0f, 100.0f);

    REQUIRE(grid.QueryRadius(100.0f, 100.0f, -1.0f).empty());
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

TEST_CASE("SpatialGrid::QueryAOI for unknown entity returns empty", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    grid.Insert(1, 50.0f, 50.0f);

    auto result = grid.QueryAOI(404);
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

TEST_CASE("AOIManager rejects invalid radius and positions", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    REQUIRE_THROWS(mgr.RegisterEntity(1, -1.0f));

    mgr.RegisterEntity(1, 50.0f);
    REQUIRE_THROWS(mgr.OnEntityMove(1, std::numeric_limits<float>::quiet_NaN(), 10.0f));
}

TEST_CASE("SpatialGrid and AOIManager reject invalid entity id", "[aoi][edge]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);
    REQUIRE_THROWS_AS(grid.Insert(engine::entity::kInvalidEntityId, 1.0f, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(grid.Update(engine::entity::kInvalidEntityId, 1.0f, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(grid.Remove(engine::entity::kInvalidEntityId),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(grid.QueryAOI(engine::entity::kInvalidEntityId),
                      std::invalid_argument);

    auto manager_grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(manager_grid));
    REQUIRE_THROWS_AS(mgr.RegisterEntity(engine::entity::kInvalidEntityId, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mgr.UpsertEntity(engine::entity::kInvalidEntityId, 1.0f, 1.0f, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mgr.UpdateEntityRadius(engine::entity::kInvalidEntityId, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mgr.OnEntityMove(engine::entity::kInvalidEntityId, 1.0f, 1.0f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mgr.UnregisterEntity(engine::entity::kInvalidEntityId),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mgr.GetVisibleEntities(engine::entity::kInvalidEntityId),
                      std::invalid_argument);
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

    mgr.OnEntityMove(1, 100.0f, 100.0f);
    mgr.OnEntityMove(2, 110.0f, 110.0f);

    auto visible1 = mgr.GetVisibleEntities(1);
    auto visible2 = mgr.GetVisibleEntities(2);

    REQUIRE(visible1.size() == 1);
    REQUIRE(visible1[0] == 2);
    REQUIRE(visible2.size() == 1);
    REQUIRE(visible2[0] == 1);
}

TEST_CASE("AOIManager updates stationary observers when a target moves", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 100.0f);
    mgr.RegisterEntity(2, 5.0f);

    mgr.OnEntityMove(1, 100.0f, 100.0f);
    mgr.OnEntityMove(2, 500.0f, 500.0f);
    REQUIRE(mgr.GetVisibleEntities(1).empty());

    mgr.OnEntityMove(2, 150.0f, 100.0f);

    auto visible1 = mgr.GetVisibleEntities(1);
    auto visible2 = mgr.GetVisibleEntities(2);
    REQUIRE(Contains(visible1, 2));
    REQUIRE_FALSE(Contains(visible2, 1));
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

    REQUIRE(enter_count == 2);
    REQUIRE(leave_count == 0);
    REQUIRE((last_observer == 1 || last_observer == 2));
    REQUIRE((last_target == 1 || last_target == 2));
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

    REQUIRE(enter_after_approach == 2);
    REQUIRE(leave_count == 2);
}

TEST_CASE("AOIManager unregister fires leave events for observer-owned visibility", "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.RegisterEntity(1, 100.0f);
    mgr.RegisterEntity(2, 5.0f);

    mgr.OnEntityMove(1, 100.0f, 100.0f);
    mgr.OnEntityMove(2, 150.0f, 100.0f);
    REQUIRE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(2), 1));

    int leave_for_1 = 0;
    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        if (!entered && observer == 1 && target == 2) {
            ++leave_for_1;
        }
    });

    mgr.UnregisterEntity(1);
    REQUIRE(leave_for_1 == 1);
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

    mgr.OnEntityMove(1, 200.0f, 200.0f);
    mgr.OnEntityMove(2, 210.0f, 210.0f);

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

TEST_CASE("SpatialGrid keeps raw out-of-bounds positions for precise radius filtering",
          "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 1500.0f, 1500.0f);

    REQUIRE(Contains(grid.QueryRadius(1500.0f, 1500.0f, 1.0f), 1));
    REQUIRE_FALSE(Contains(grid.QueryRadius(999.0f, 999.0f, 10.0f), 1));
}

TEST_CASE("SpatialGrid::QueryAOI is only a nine-cell candidate query", "[aoi][spatial_grid]") {
    SpatialGrid grid(1000.0f, 1000.0f, 100.0f);

    grid.Insert(1, 50.0f, 50.0f);
    grid.Insert(2, 250.0f, 50.0f);

    REQUIRE_FALSE(Contains(grid.QueryAOI(1), 2));
    REQUIRE(Contains(grid.QueryRadius(50.0f, 50.0f, 250.0f), 2));
}

TEST_CASE("AOIManager raw radius query includes self while visible set excludes self",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 100.0f, 100.0f, 50.0f);

    REQUIRE(Contains(mgr.QueryRadius(100.0f, 100.0f, 0.0f), 1));
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(1), 1));
}

TEST_CASE("AOIManager supports zero-radius visibility at the same coordinate",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 100.0f, 100.0f, 0.0f);
    mgr.UpsertEntity(2, 100.0f, 100.0f, 0.0f);
    mgr.UpsertEntity(3, 100.01f, 100.0f, 0.0f);

    REQUIRE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(1), 3));
}

TEST_CASE("AOIManager upsert updates radius and position without old-position transient events",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 0.0f, 0.0f, 1.0f);
    mgr.UpsertEntity(2, 50.0f, 0.0f, 1.0f);
    mgr.UpsertEntity(3, 200.0f, 0.0f, 1.0f);

    std::vector<EventRecord> events;
    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        events.push_back(EventRecord{observer, target, entered});
    });

    mgr.UpsertEntity(1, 200.0f, 0.0f, 60.0f);

    REQUIRE_FALSE(HasEvent(events, 1, 2, true));
    REQUIRE_FALSE(HasEvent(events, 1, 2, false));
    REQUIRE(HasEvent(events, 1, 3, true));
    REQUIRE(Contains(mgr.GetVisibleEntities(1), 3));
    REQUIRE_FALSE(Contains(mgr.QueryRadius(0.0f, 0.0f, 1.0f), 1));
}

TEST_CASE("AOIManager update_radius changes only observer-side visibility",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 100.0f, 100.0f, 10.0f);
    mgr.UpsertEntity(2, 130.0f, 100.0f, 10.0f);
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(1), 2));

    mgr.UpdateEntityRadius(1, 40.0f);
    REQUIRE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(2), 1));

    mgr.UpdateEntityRadius(1, 5.0f);
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(1), 2));
}

TEST_CASE("AOIManager keeps affected observer queries correct after max radius shrink",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 0.0f, 0.0f, 500.0f);
    mgr.UpsertEntity(2, 300.0f, 0.0f, 100.0f);
    mgr.UpsertEntity(3, 800.0f, 0.0f, 1.0f);
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(2), 3));

    mgr.UpdateEntityRadius(1, 10.0f);
    mgr.OnEntityMove(3, 250.0f, 0.0f);

    REQUIRE(Contains(mgr.GetVisibleEntities(2), 3));
}

TEST_CASE("AOIManager keeps affected observer queries correct after unregistering max radius",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 0.0f, 0.0f, 500.0f);
    mgr.UpsertEntity(2, 300.0f, 0.0f, 100.0f);
    mgr.UpsertEntity(3, 800.0f, 0.0f, 1.0f);
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(2), 3));

    mgr.UnregisterEntity(1);
    mgr.OnEntityMove(3, 250.0f, 0.0f);

    REQUIRE(Contains(mgr.GetVisibleEntities(2), 3));
}

TEST_CASE("AOIManager upsert shrinking max radius preserves remaining observer reach",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 0.0f, 0.0f, 500.0f);
    mgr.UpsertEntity(2, 300.0f, 0.0f, 100.0f);
    mgr.UpsertEntity(3, 800.0f, 0.0f, 1.0f);
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(2), 3));

    mgr.UpsertEntity(1, 0.0f, 0.0f, 10.0f);
    mgr.OnEntityMove(3, 250.0f, 0.0f);

    REQUIRE(Contains(mgr.GetVisibleEntities(2), 3));
}

TEST_CASE("AOIManager unregister uses watchers to clear reverse visibility",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 100.0f, 100.0f, 100.0f);
    mgr.UpsertEntity(2, 130.0f, 100.0f, 100.0f);
    REQUIRE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE(Contains(mgr.GetVisibleEntities(2), 1));

    std::vector<EventRecord> events;
    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        events.push_back(EventRecord{observer, target, entered});
    });

    mgr.UnregisterEntity(2);

    REQUIRE(HasEvent(events, 1, 2, false));
    REQUIRE(HasEvent(events, 2, 1, false));
    REQUIRE_FALSE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE_FALSE(Contains(mgr.QueryRadius(130.0f, 100.0f, 1.0f), 2));
}

TEST_CASE("AOIManager dispatches sorted multi-target events for one observer",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(3, 30.0f, 0.0f, 0.0f);
    mgr.UpsertEntity(1, 10.0f, 0.0f, 0.0f);
    mgr.UpsertEntity(2, 20.0f, 0.0f, 0.0f);

    std::vector<EventRecord> events;
    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        events.push_back(EventRecord{observer, target, entered});
    });

    mgr.UpsertEntity(10, 0.0f, 0.0f, 100.0f);

    std::vector<EntityId> entered_targets;
    for (const auto& event : events) {
        if (event.observer == 10 && event.entered) {
            entered_targets.push_back(event.target);
        }
    }
    REQUIRE(entered_targets == std::vector<EntityId>{1, 2, 3});

    events.clear();
    mgr.OnEntityMove(10, 900.0f, 900.0f);

    std::vector<EntityId> left_targets;
    for (const auto& event : events) {
        if (event.observer == 10 && !event.entered) {
            left_targets.push_back(event.target);
        }
    }
    REQUIRE(left_targets == std::vector<EntityId>{1, 2, 3});
}

TEST_CASE("AOIManager rejects C++ callback reentrant mutations and preserves state",
          "[aoi][aoi_manager]") {
    auto grid = std::make_unique<SpatialGrid>(1000.0f, 1000.0f, 100.0f);
    AOIManager mgr(std::move(grid));

    mgr.UpsertEntity(1, 100.0f, 100.0f, 100.0f);

    bool blocked = false;
    mgr.SetEventCallback([&](EntityId observer, EntityId target, bool entered) {
        if (observer == 1 && target == 2 && entered) {
            try {
                mgr.OnEntityMove(1, 900.0f, 900.0f);
            } catch (const std::logic_error&) {
                blocked = true;
            }
        }
    });

    mgr.UpsertEntity(2, 120.0f, 100.0f, 10.0f);

    REQUIRE(blocked);
    REQUIRE(Contains(mgr.GetVisibleEntities(1), 2));
    REQUIRE(Contains(mgr.QueryRadius(100.0f, 100.0f, 1.0f), 1));
}

TEST_CASE("Lua AOI binding exports module and symmetric callbacks", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "events = {}\n"
        "aoi.set_event_callback(function(observer, target, entered)\n"
        "  events[#events + 1] = observer .. ':' .. target .. ':' .. tostring(entered)\n"
        "end)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "aoi.register_entity(2, 110, 110, 50)\n"
        "return #events .. ',' .. #aoi.get_visible(1) .. ',' .. #aoi.get_visible(2)",
        result));

    REQUIRE(result == "2,1,1");
}

TEST_CASE("Lua AOI binding isolates state per ScriptVM", "[aoi][bind]") {
    AOIBindFixture f1;
    AOIBindFixture f2;
    std::string result1;
    std::string result2;

    REQUIRE(f1.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "return aoi.count()",
        result1));
    REQUIRE(f2.RunLuaResult("return aoi.count()", result2));

    REQUIRE(result1 == "1");
    REQUIRE(result2 == "0");
}

TEST_CASE("Lua AOI binding catches callback errors and restores stack", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "aoi.set_event_callback(function() error('intentional AOI callback error') end)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "aoi.register_entity(2, 110, 110, 50)\n"
        "aoi.set_event_callback(nil)\n"
        "return aoi.count()",
        result));

    REQUIRE(result == "2");
}

TEST_CASE("Lua AOI binding rejects mutation from callbacks", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "blocked = {}\n"
        "aoi.set_event_callback(function()\n"
        "  local calls = {\n"
        "    function() return aoi.init(1000, 1000, 100) end,\n"
        "    function() return aoi.register_entity(3, 300, 300, 10) end,\n"
        "    function() return aoi.update_entity(1, 200, 200) end,\n"
        "    function() return aoi.update_radius(1, 20) end,\n"
        "    function() return aoi.unregister_entity(2) end,\n"
        "    function() return aoi.set_event_callback(nil) end,\n"
        "    function() return aoi.shutdown() end,\n"
        "  }\n"
        "  for i, fn in ipairs(calls) do\n"
        "    local ok, err = fn()\n"
        "    blocked[i] = ok == nil and string.find(err, 'mutation') ~= nil\n"
        "  end\n"
        "end)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "aoi.register_entity(2, 110, 110, 50)\n"
        "local out = {}\n"
        "for i = 1, 7 do out[i] = tostring(blocked[i]) end\n"
        "return table.concat(out, ',') .. ',' .. aoi.count()",
        result));

    REQUIRE(result == "true,true,true,true,true,true,true,2");
}

TEST_CASE("Lua AOI binding rejects invalid init parameters", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "local ok, err = pcall(function() aoi.init(100, 100, 0) end)\n"
        "return tostring(ok) .. ',' .. tostring(string.find(err, 'cell_size') ~= nil)",
        result));

    REQUIRE(result == "false,true");
}

TEST_CASE("Lua AOI binding init reset clears entities and old callback", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "events = 0\n"
        "aoi.set_event_callback(function() events = events + 1 end)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "aoi.init(500, 500, 50)\n"
        "local after_reset = aoi.count()\n"
        "aoi.register_entity(2, 100, 100, 50)\n"
        "return after_reset .. ',' .. aoi.count() .. ',' .. events",
        result));

    REQUIRE(result == "0,1,0");
}

TEST_CASE("Lua AOI binding failed init preserves old manager state", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "local ok, err = aoi.init(1e20, 1000, 1)\n"
        "return tostring(ok == nil) .. ',' .. tostring(string.find(err, 'AOI init failed') ~= nil)"
        " .. ',' .. aoi.count()",
        result));

    REQUIRE(result == "true,true,1");
}

TEST_CASE("Lua AOI binding shutdown leaves query APIs in empty state", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "aoi.register_entity(1, 100, 100, 50)\n"
        "aoi.shutdown()\n"
        "return aoi.count() .. ',' .. #aoi.get_visible(1) .. ',' .. #aoi.query_radius(100, 100, 10)",
        result));

    REQUIRE(result == "0,0,0");
}

TEST_CASE("Lua AOI binding update_radius changes observer visibility", "[aoi][bind]") {
    AOIBindFixture f;
    std::string result;

    REQUIRE(f.RunLuaResult(
        "aoi.init(1000, 1000, 100)\n"
        "aoi.register_entity(1, 100, 100, 10)\n"
        "aoi.register_entity(2, 130, 100, 10)\n"
        "local before = #aoi.get_visible(1)\n"
        "aoi.update_radius(1, 40)\n"
        "local after = #aoi.get_visible(1)\n"
        "return before .. ',' .. after",
        result));

    REQUIRE(result == "0,1");
}
