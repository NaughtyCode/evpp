#include "runtime/aoi/aoi_manager.h"

#include <algorithm>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace aoi {

AOIManager::AOIManager(std::unique_ptr<SpatialGrid> grid)
	: grid_(std::move(grid)) {
}

void AOIManager::RegisterEntity(entity::EntityId id, float aoi_radius) {
	ENGINE_PROFILE_AOI_REGISTER();
	aoi_radii_[id] = aoi_radius;
	visible_[id] = {};
}

void AOIManager::UnregisterEntity(entity::EntityId id) {
	ENGINE_PROFILE_AOI_UNREGISTER();
	grid_->Remove(id);
	aoi_radii_.erase(id);
	entity_x_.erase(id);
	entity_y_.erase(id);

	// Remove from other entities' visibility sets
	for (auto& [other_id, vis_set] : visible_) {
		if (vis_set.erase(id) && event_callback_) {
			event_callback_(other_id, id, false);
		}
	}
	visible_.erase(id);
}

void AOIManager::OnEntityMove(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_MOVE();
	if (aoi_radii_.find(id) == aoi_radii_.end()) return;

	float old_x = entity_x_[id];
	float old_y = entity_y_[id];
	entity_x_[id] = x;
	entity_y_[id] = y;

	grid_->Update(id, x, y);

	// Only recompute visibility if moved enough (more than 10% of cell size)
	// or always for correctness with enter/leave events.
	RecomputeVisibility(id);
}

std::vector<entity::EntityId> AOIManager::GetVisibleEntities(entity::EntityId id) const {
	auto it = visible_.find(id);
	if (it == visible_.end()) return {};
	return std::vector<entity::EntityId>(it->second.begin(), it->second.end());
}

std::vector<entity::EntityId> AOIManager::QueryRadius(float x, float y, float radius) const {
	ENGINE_PROFILE_AOI_QUERY();
	return grid_->QueryRadius(x, y, radius);
}

void AOIManager::SetEventCallback(AOIEventCallback callback) {
	event_callback_ = std::move(callback);
}

void AOIManager::RecomputeVisibility(entity::EntityId id) {
	ENGINE_PROFILE_AOI_VISIBILITY();
	auto it = aoi_radii_.find(id);
	if (it == aoi_radii_.end()) return;

	float radius = it->second;
	float x = entity_x_[id];
	float y = entity_y_[id];

	auto candidates = grid_->QueryRadius(x, y, radius);

	std::unordered_set<entity::EntityId> new_visible;
	for (auto other : candidates) {
		if (other == id) continue;
		new_visible.insert(other);
	}

	auto& old_visible = visible_[id];

	// Fire enter events for newly visible entities
	for (auto other : new_visible) {
		if (old_visible.find(other) == old_visible.end() && event_callback_) {
			event_callback_(id, other, true);
		}
	}

	// Fire leave events for entities no longer visible
	for (auto other : old_visible) {
		if (new_visible.find(other) == new_visible.end() && event_callback_) {
			event_callback_(id, other, false);
		}
	}

	old_visible = std::move(new_visible);
}

}  // namespace aoi
}  // namespace engine
