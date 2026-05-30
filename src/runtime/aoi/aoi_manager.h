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

// Callback invoked when an entity enters or leaves another's AOI radius.
// observer: the entity whose AOI detects the change
// target: the entity that entered or left the observer's AOI
// entered: true if target entered, false if target left
using AOIEventCallback = std::function<void(entity::EntityId observer,
											 entity::EntityId target,
											 bool entered)>;

// AOIManager wraps SpatialGrid with AOI radius tracking and enter/leave events.
// Each entity has an AOI radius. When entities move, the manager compares
// visibility sets and fires callbacks for entities that entered or left.
class CLOUD_ENGINE_API AOIManager {
public:
	explicit AOIManager(std::unique_ptr<SpatialGrid> grid);

	// Register entity with its interest radius.
	void RegisterEntity(entity::EntityId id, float aoi_radius);

	// Unregister entity, removing from grid and visibility tracking.
	void UnregisterEntity(entity::EntityId id);

	// Called when entity position changes.
	void OnEntityMove(entity::EntityId id, float x, float y);

	// Get entities currently visible to this entity.
	std::vector<entity::EntityId> GetVisibleEntities(entity::EntityId id) const;

	// Get entities within radius of a point.
	std::vector<entity::EntityId> QueryRadius(float x, float y, float radius) const;

	// Subscribe to enter/leave events.
	void SetEventCallback(AOIEventCallback callback);

	// Total registered entity count.
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

	std::unique_ptr<SpatialGrid> grid_;
	AOIEventCallback event_callback_;

	std::unordered_map<entity::EntityId, float> aoi_radii_;
	std::unordered_map<entity::EntityId, Position> positions_;
	std::unordered_map<entity::EntityId, std::unordered_set<entity::EntityId>> visible_;
	float max_aoi_radius_ = 0.0f;
};

}  // namespace aoi
}  // namespace engine
