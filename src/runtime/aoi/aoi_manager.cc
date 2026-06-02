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

void ValidateEntityId(entity::EntityId id) {
	if (id == entity::kInvalidEntityId) {
		throw std::invalid_argument("AOI entity id must be valid");
	}
}

}  /* namespace */

AOIManager::AOIManager(std::unique_ptr<SpatialGrid> grid)
	: grid_(std::move(grid)) {
	if (!grid_) {
		throw std::invalid_argument("AOIManager requires a SpatialGrid");
	}
}

void AOIManager::RegisterEntity(entity::EntityId id, float aoi_radius) {
	ENGINE_PROFILE_AOI_REGISTER();
	EnsureCanMutate();
	ValidateEntityId(id);
	ValidateRadius(aoi_radius);

	const bool was_registered = aoi_radii_.find(id) != aoi_radii_.end();
	if (!was_registered) {
		aoi_radii_[id] = aoi_radius;
		visible_.try_emplace(id);
		return;
	}

	UpdateEntityRadius(id, aoi_radius);
}

void AOIManager::UpsertEntity(entity::EntityId id, float x, float y, float aoi_radius) {
	ENGINE_PROFILE_AOI_REGISTER();
	EnsureCanMutate();
	ValidateEntityId(id);
	ValidateRadius(aoi_radius);
	ValidatePosition(x, y);

	const auto old_radius_it = aoi_radii_.find(id);
	const bool was_registered = old_radius_it != aoi_radii_.end();
	const float old_radius = was_registered ? old_radius_it->second : 0.0f;
	const auto old_position_it = positions_.find(id);
	const bool had_old_position = old_position_it != positions_.end();
	const Position old_position = had_old_position ? old_position_it->second : Position{};

	aoi_radii_[id] = aoi_radius;
	positions_[id] = Position{x, y};
	visible_.try_emplace(id);
	grid_->Update(id, x, y);

	if (aoi_radius > max_aoi_radius_) {
		max_aoi_radius_ = aoi_radius;
	} else if (was_registered && old_radius == max_aoi_radius_ &&
			   aoi_radius < old_radius) {
		RecomputeMaxAOIRadius();
	}

	/**
	 * Upsert is atomic from the event stream's point of view: radius and
	 * position are installed before any observer recompute runs, so callers do
	 * not see transient enter/leave events from the old position.
	 */
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

void AOIManager::UpdateEntityRadius(entity::EntityId id, float aoi_radius) {
	ENGINE_PROFILE_AOI_REGISTER();
	EnsureCanMutate();
	ValidateEntityId(id);
	ValidateRadius(aoi_radius);

	auto radius_it = aoi_radii_.find(id);
	if (radius_it == aoi_radii_.end()) return;

	const float old_radius = radius_it->second;
	radius_it->second = aoi_radius;
	if (aoi_radius > max_aoi_radius_) {
		max_aoi_radius_ = aoi_radius;
	} else if (old_radius == max_aoi_radius_ && aoi_radius < old_radius) {
		RecomputeMaxAOIRadius();
	}

	auto events = RecomputeVisibility(id);
	DispatchEvents(events);
}

void AOIManager::UnregisterEntity(entity::EntityId id) {
	ENGINE_PROFILE_AOI_UNREGISTER();
	EnsureCanMutate();
	ValidateEntityId(id);
	auto radius_it = aoi_radii_.find(id);
	if (radius_it == aoi_radii_.end()) return;

	std::vector<AOIEvent> events;
	auto visible_it = visible_.find(id);
	if (visible_it != visible_.end()) {
		std::vector<entity::EntityId> targets(visible_it->second.begin(), visible_it->second.end());
		std::sort(targets.begin(), targets.end());
		events.reserve(targets.size());
		for (auto target : targets) {
			RemoveWatcher(target, id);
			events.push_back(AOIEvent{id, target, false});
		}
	}

	auto watchers_it = watchers_.find(id);
	if (watchers_it != watchers_.end()) {
		std::vector<entity::EntityId> observers(watchers_it->second.begin(), watchers_it->second.end());
		std::sort(observers.begin(), observers.end());
		for (auto observer : observers) {
			auto observer_visible_it = visible_.find(observer);
			if (observer_visible_it != visible_.end() && observer_visible_it->second.erase(id)) {
				events.push_back(AOIEvent{observer, id, false});
			}
		}
		watchers_.erase(watchers_it);
	}

	grid_->Remove(id);
	const float old_radius = radius_it->second;
	aoi_radii_.erase(radius_it);
	positions_.erase(id);
	visible_.erase(id);

	if (old_radius == max_aoi_radius_) {
		RecomputeMaxAOIRadius();
	}

	DispatchEvents(events);
}

void AOIManager::OnEntityMove(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_MOVE();
	EnsureCanMutate();
	ValidateEntityId(id);
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
	ValidateEntityId(id);
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
	EnsureCanMutate();
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
	std::vector<entity::EntityId> ordered_new(new_visible.begin(), new_visible.end());
	std::vector<entity::EntityId> ordered_old(old_visible.begin(), old_visible.end());
	std::sort(ordered_new.begin(), ordered_new.end());
	std::sort(ordered_old.begin(), ordered_old.end());

	/**
	 * Keep visible_ and watchers_ as an exact bidirectional relation. Sorting
	 * makes the event batch deterministic for one observer even though the raw
	 * spatial query itself has no ordering contract.
	 */
	for (auto other : ordered_new) {
		if (old_visible.find(other) == old_visible.end()) {
			AddWatcher(other, id);
			events.push_back(AOIEvent{id, other, true});
		}
	}

	for (auto other : ordered_old) {
		if (new_visible.find(other) == new_visible.end()) {
			RemoveWatcher(other, id);
			events.push_back(AOIEvent{id, other, false});
		}
	}

	old_visible = std::move(new_visible);
	return events;
}

void AOIManager::DispatchEvents(const std::vector<AOIEvent>& events) {
	if (!event_callback_ || events.empty()) return;
	auto callback = event_callback_;
	/**
	 * Synchronous callbacks may read AOI state, but writes would invalidate the
	 * batch being dispatched. The guard makes such writes fail consistently.
	 */
	dispatching_events_ = true;
	try {
		for (const auto& event : events) {
			callback(event.observer, event.target, event.entered);
		}
		dispatching_events_ = false;
	} catch (...) {
		dispatching_events_ = false;
		throw;
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

void AOIManager::EnsureCanMutate() const {
	if (dispatching_events_) {
		throw std::logic_error("AOI mutation is not allowed from AOI callback");
	}
}

void AOIManager::RemoveWatcher(entity::EntityId target, entity::EntityId observer) {
	auto watchers_it = watchers_.find(target);
	if (watchers_it == watchers_.end()) return;
	watchers_it->second.erase(observer);
	if (watchers_it->second.empty()) {
		watchers_.erase(watchers_it);
	}
}

void AOIManager::AddWatcher(entity::EntityId target, entity::EntityId observer) {
	watchers_[target].insert(observer);
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

}  /* namespace aoi */
}  /* namespace engine */
