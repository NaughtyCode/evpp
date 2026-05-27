#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/entity/entity_id.h"

namespace engine {
namespace aoi {

// Grid-based spatial index for AOI queries.
// Divides the world into fixed-size cells for O(1) insert/update/remove
// and O(cell_contents) range queries.
class ENGINE_API SpatialGrid {
public:
	SpatialGrid(float world_width, float world_height, float cell_size);

	// Add/update/remove an entity's position.
	void Insert(entity::EntityId id, float x, float y);
	void Update(entity::EntityId id, float x, float y);
	void Remove(entity::EntityId id);

	// Query entities within radius of a point.
	std::vector<entity::EntityId> QueryRadius(float x, float y, float radius) const;

	// Get entities in same cell + neighboring cells (9-cell AOI).
	std::vector<entity::EntityId> QueryAOI(entity::EntityId id) const;

	// Get entities in same cell + neighboring cells for a position.
	std::vector<entity::EntityId> QueryAOIAt(float x, float y) const;

	// Total entity count in the grid.
	size_t Size() const { return entity_cell_.size(); }

	// Clear all entities.
	void Clear();

private:
	int CellIndex(int col, int row) const;
	void CellIndices(float x, float y, float radius,
					 int& min_col, int& min_row,
					 int& max_col, int& max_row) const;
	void GetPosition(entity::EntityId id, float& x, float& y) const;

	float cell_size_;
	float inv_cell_size_;
	int cols_;
	int rows_;
	float world_width_;
	float world_height_;

	std::vector<std::vector<entity::EntityId>> grid_;
	std::unordered_map<entity::EntityId, int> entity_cell_;
	std::unordered_map<entity::EntityId, float> entity_x_;
	std::unordered_map<entity::EntityId, float> entity_y_;
};

}  // namespace aoi
}  // namespace engine
