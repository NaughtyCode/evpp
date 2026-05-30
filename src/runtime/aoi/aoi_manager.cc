#include "runtime/aoi/aoi_manager.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace aoi {

namespace {

bool IsValidRadius(float radius) {
	return std::isfinite(radius) && radius >= 0.0f;
}

void ValidateRadius(float radius) {
	if (!IsValidRadius(radius)) {
		throw std::invalid_argument("AOI radius must be finite and non-negative");
	}
}

void ValidatePosition(float x, float y) {
	if (!std::isfinite(x) || !std::isfinite(y)) {
		throw std::invalid_argument("AOI positions must be finite");
	}
}

}  // namespace

AOIManager::AOIManager(std::unique_ptr<SpatialGrid> grid)
	: grid_(std::move(grid)) {
	if (!grid_) {
		throw std::invalid_argument("AOIManager requires a SpatialGrid");
	}
}

void AOIManager::RegisterEntity(entity::EntityId id, float aoi_radius) {
	ENGINE_PROFILE_AOI_REGISTER();
	ValidateRadius(aoi_radius);

	const auto old_radius_it = aoi_radii_.find(id);
	const bool was_registered = old_radius_it != aoi_radii_.end();
	const float old_radius = was_registered ? old_radius_it->second : 0.0f;
	const bool has_position = positions_.find(id) != positions_.end();

	aoi_radii_[id] = aoi_radius;
	visible_.try_emplace(id);

	if (has_position && aoi_radius > max_aoi_radius_) {
		max_aoi_radius_ = aoi_radius;
	} else if (has_position && was_registered && old_radius == max_aoi_radius_ &&
			   aoi_radius < old_radius) {
		RecomputeMaxAOIRadius();
	}

	if (has_position) {
		auto events = RecomputeVisibility(id);
		DispatchEvents(events);
	}
}

void AOIManager::UnregisterEntity(entity::EntityId id) {
	ENGINE_PROFILE_AOI_UNREGISTER();
	auto radius_it = aoi_radii_.find(id);
	if (radius_it == aoi_radii_.end()) return;

	std::vector<AOIEvent> events;
	auto visible_it = visible_.find(id);
	if (visible_it != visible_.end()) {
		events.reserve(visible_it->second.size());
		for (auto target : visible_it->second) {
			events.push_back(AOIEvent{id, target, false});
		}
	}

	grid_->Remove(id);
	const float old_radius = radius_it->second;
	aoi_radii_.erase(radius_it);
	positions_.erase(id);

	for (auto& [other_id, vis_set] : visible_) {
		if (other_id == id) continue;
		if (vis_set.erase(id)) {
			events.push_back(AOIEvent{other_id, id, false});
		}
	}
	visible_.erase(id);

	if (old_radius == max_aoi_radius_) {
		RecomputeMaxAOIRadius();
	}

	DispatchEvents(events);
}

void AOIManager::OnEntityMove(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_MOVE();
	if (aoi_radii_.find(id) == aoi_radii_.end()) return;
	ValidatePosition(x, y);

	const auto old_position_it = positions_.find(id);
	const bool had_old_position = old_position_it != positions_.end();
	const Position old_position = had_old_position ? old_position_it->second : Position{};
	positions_[id] = Position{x, y};
	const float radius = aoi_radii_.at(id);
	if (radius > max_aoi_radius_) {
		max_aoi_radius_ = radius;
	}

	grid_->Update(id, x, y);

	std::unordered_set<entity::EntityId> affected;
	affected.insert(id);
	if (had_old_position) {
		AddObserversNear(old_position.x, old_position.y, affected);
	}
	AddObserversNear(x, y, affected);

	std::vector<entity::EntityId> ordered_affected(affected.begin(), affected.end());
	std::sort(ordered_affected.begin(), ordered_affected.end());

	std::vector<AOIEvent> events;
	for (auto observer : ordered_affected) {
		auto observer_events = RecomputeVisibility(observer);
		events.insert(events.end(), observer_events.begin(), observer_events.end());
	}
	DispatchEvents(events);
}

std::vector<entity::EntityId> AOIManager::GetVisibleEntities(entity::EntityId id) const {
	auto it = visible_.find(id);
	if (it == visible_.end()) return {};
	std::vector<entity::EntityId> result(it->second.begin(), it->second.end());
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<entity::EntityId> AOIManager::QueryRadius(float x, float y, float radius) const {
	ENGINE_PROFILE_AOI_QUERY();
	return grid_->QueryRadius(x, y, radius);
}

void AOIManager::SetEventCallback(AOIEventCallback callback) {
	event_callback_ = std::move(callback);
}

std::vector<AOIManager::AOIEvent> AOIManager::RecomputeVisibility(entity::EntityId id) {
	ENGINE_PROFILE_AOI_VISIBILITY();
	std::vector<AOIEvent> events;
	auto it = aoi_radii_.find(id);
	if (it == aoi_radii_.end()) return events;

	float radius = it->second;
	auto position_it = positions_.find(id);
	if (position_it == positions_.end()) return events;
	const Position position = position_it->second;

	auto candidates = grid_->QueryRadius(position.x, position.y, radius);

	std::unordered_set<entity::EntityId> new_visible;
	for (auto other : candidates) {
		if (other == id) continue;
		new_visible.insert(other);
	}

	auto& old_visible = visible_[id];

	for (auto other : new_visible) {
		if (old_visible.find(other) == old_visible.end()) {
			events.push_back(AOIEvent{id, other, true});
		}
	}

	for (auto other : old_visible) {
		if (new_visible.find(other) == new_visible.end()) {
			events.push_back(AOIEvent{id, other, false});
		}
	}

	old_visible = std::move(new_visible);
	return events;
}

void AOIManager::DispatchEvents(const std::vector<AOIEvent>& events) {
	if (!event_callback_) return;
	auto callback = event_callback_;
	for (const auto& event : events) {
		callback(event.observer, event.target, event.entered);
	}
}

void AOIManager::RecomputeMaxAOIRadius() {
	max_aoi_radius_ = 0.0f;
	for (const auto& [id, _] : positions_) {
		auto radius_it = aoi_radii_.find(id);
		if (radius_it != aoi_radii_.end()) {
			max_aoi_radius_ = std::max(max_aoi_radius_, radius_it->second);
		}
	}
}

void AOIManager::AddObserversNear(
	float x, float y, std::unordered_set<entity::EntityId>& observers) const {
	if (max_aoi_radius_ < 0.0f) return;

	auto candidates = grid_->QueryRadius(x, y, max_aoi_radius_);
	for (auto candidate : candidates) {
		if (aoi_radii_.find(candidate) != aoi_radii_.end() &&
			positions_.find(candidate) != positions_.end()) {
			observers.insert(candidate);
		}
	}
}

}  // namespace aoi
}  // namespace engine
