#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/entity/entity_id.h"

namespace engine {
namespace aoi {

/**
 * Grid-based spatial index for bounded 2D AOI queries.
 *
 * The grid clamps coordinates only when choosing a cell. Stored entity
 * positions keep the original x/y values, so precise radius checks still use
 * the unclamped coordinates. Query result order follows cell scan and vector
 * storage order and is intentionally not a protocol contract.
 */
class CLOUD_ENGINE_API SpatialGrid {
public:
	SpatialGrid(float world_width, float world_height, float cell_size);

	/** Add or replace an entity's position in the spatial grid. */
	void Insert(entity::EntityId id, float x, float y);

	/** Move an entity, inserting it when the id is not present. */
	void Update(entity::EntityId id, float x, float y);

	/** Remove an entity from the spatial grid. Missing ids are ignored. */
	void Remove(entity::EntityId id);

	/** Return every entity within radius of a point, including any caller id. */
	std::vector<entity::EntityId> QueryRadius(float x, float y, float radius) const;

	/** Return same-cell and neighboring-cell candidates for an entity.
	 * Unknown ids return empty; kInvalidEntityId is rejected.
	 */
	std::vector<entity::EntityId> QueryAOI(entity::EntityId id) const;

	/** Return same-cell and neighboring-cell candidates for a position. */
	std::vector<entity::EntityId> QueryAOIAt(float x, float y) const;

	/** Return the total entity count in the grid. */
	size_t Size() const { return entity_cell_.size(); }

	/** Remove all entities while keeping the allocated cell array reusable. */
	void Clear();

private:
	struct Position {
		float x = 0.0f;
		float y = 0.0f;
	};

	struct CellEntry {
		entity::EntityId id = entity::kInvalidEntityId;
		Position position;
	};

	struct CellRef {
		int cell_index = 0;
		size_t entry_index = 0;
	};

	int CellIndex(int col, int row) const;
	int CellIndexForPosition(float x, float y) const;
	void CellIndices(float x, float y, float radius,
					 int& min_col, int& min_row,
					 int& max_col, int& max_row) const;
	bool TryGetPosition(entity::EntityId id, Position& position) const;
	void RemoveCellEntry(const CellRef& ref);

	float cell_size_;
	float inv_cell_size_;
	int cols_;
	int rows_;
	float world_width_;
	float world_height_;

	std::vector<std::vector<CellEntry>> grid_;
	std::unordered_map<entity::EntityId, CellRef> entity_cell_;
};

}  /* namespace aoi */
}  /* namespace engine */
