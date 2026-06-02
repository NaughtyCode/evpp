#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/aoi/spatial_index.h"
#include "runtime/core/engine_api.h"
#include "runtime/entity/entity_id.h"

namespace engine {
namespace aoi {

/**
 * Callback invoked when a target enters or leaves an observer's AOI radius.
 *
 * Visibility is directional: observer seeing target does not imply target
 * seeing observer. The callback is synchronous and must not call mutating
 * AOIManager APIs on the same manager.
 */
using AOIEventCallback = std::function<void(entity::EntityId observer,
											 entity::EntityId target,
											 bool entered)>;

/**
 * Single-zone AOI manager backed by a fixed 2D SpatialGrid.
 *
 * Each registered entity owns an observer-side AOI radius. visible_[observer]
 * stores the targets currently visible to that observer, while watchers_[target]
 * stores the reverse relation so unregistering a target does not require a full
 * visible-table scan. This class is not internally thread-safe; callers must
 * provide single-thread or phase ownership.
 */
class CLOUD_ENGINE_API AOIManager {
public:
	explicit AOIManager(std::unique_ptr<SpatialGrid> grid);

	/** Register an entity radius without assigning a position. */
	void RegisterEntity(entity::EntityId id, float aoi_radius);

	/** Register or update radius and position as one atomic AOI mutation. */
	void UpsertEntity(entity::EntityId id, float x, float y, float aoi_radius);

	/** Update only the observer-side AOI radius for a registered entity. */
	void UpdateEntityRadius(entity::EntityId id, float aoi_radius);

	/** Unregister entity, removing it from grid and both visibility tables. */
	void UnregisterEntity(entity::EntityId id);

	/** Move a registered entity and recompute affected observer visibility. */
	void OnEntityMove(entity::EntityId id, float x, float y);

	/** Return sorted targets visible to this observer, excluding self.
	 * Unknown ids return empty; kInvalidEntityId is rejected.
	 */
	std::vector<entity::EntityId> GetVisibleEntities(entity::EntityId id) const;

	/** Return raw radius-query results from the spatial grid. */
	std::vector<entity::EntityId> QueryRadius(float x, float y, float radius) const;

	/** Subscribe to synchronous enter/leave events. */
	void SetEventCallback(AOIEventCallback callback);

	/** Return the number of registered AOI entities. */
	size_t EntityCount() const { return aoi_radii_.size(); }

private:
	struct Position {
		float x = 0.0f;
		float y = 0.0f;
	};

	struct AOIEvent {
		entity::EntityId observer = entity::kInvalidEntityId;
		entity::EntityId target = entity::kInvalidEntityId;
		bool entered = false;
	};

	std::vector<AOIEvent> RecomputeVisibility(entity::EntityId id);
	void DispatchEvents(const std::vector<AOIEvent>& events);
	void RecomputeMaxAOIRadius();
	void AddObserversNear(float x, float y, std::unordered_set<entity::EntityId>& observers) const;
	void EnsureCanMutate() const;
	void RemoveWatcher(entity::EntityId target, entity::EntityId observer);
	void AddWatcher(entity::EntityId target, entity::EntityId observer);

	std::unique_ptr<SpatialGrid> grid_;
	AOIEventCallback event_callback_;

	std::unordered_map<entity::EntityId, float> aoi_radii_;
	std::unordered_map<entity::EntityId, Position> positions_;
	std::unordered_map<entity::EntityId, std::unordered_set<entity::EntityId>> visible_;
	std::unordered_map<entity::EntityId, std::unordered_set<entity::EntityId>> watchers_;
	float max_aoi_radius_ = 0.0f;
	bool dispatching_events_ = false;
};

}  /* namespace aoi */
}  /* namespace engine */
