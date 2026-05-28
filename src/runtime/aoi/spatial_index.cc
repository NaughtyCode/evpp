#include "runtime/aoi/spatial_index.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace aoi {

SpatialGrid::SpatialGrid(float world_width, float world_height, float cell_size)
	: cell_size_(cell_size)
	, inv_cell_size_(1.0f / cell_size)
	, world_width_(world_width)
	, world_height_(world_height) {
	cols_ = static_cast<int>(std::ceil(world_width * inv_cell_size_));
	rows_ = static_cast<int>(std::ceil(world_height * inv_cell_size_));
	if (cols_ < 1) cols_ = 1;
	if (rows_ < 1) rows_ = 1;
	grid_.resize(cols_ * rows_);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "SpatialGrid: [{}]x[{}] cells, cell_size=[{}]",
					cols_, rows_, cell_size_);
}

int SpatialGrid::CellIndex(int col, int row) const {
	return row * cols_ + col;
}

void SpatialGrid::CellIndices(float x, float y, float radius,
							   int& min_col, int& min_row,
							   int& max_col, int& max_row) const {
	min_col = std::max(0, static_cast<int>((x - radius) * inv_cell_size_));
	min_row = std::max(0, static_cast<int>((y - radius) * inv_cell_size_));
	max_col = std::min(cols_ - 1, static_cast<int>((x + radius) * inv_cell_size_));
	max_row = std::min(rows_ - 1, static_cast<int>((y + radius) * inv_cell_size_));
}

void SpatialGrid::GetPosition(entity::EntityId id, float& x, float& y) const {
	auto xit = entity_x_.find(id);
	auto yit = entity_y_.find(id);
	x = (xit != entity_x_.end()) ? xit->second : 0.0f;
	y = (yit != entity_y_.end()) ? yit->second : 0.0f;
}

void SpatialGrid::Insert(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_GRID_INSERT();
	int col = std::clamp(static_cast<int>(x * inv_cell_size_), 0, cols_ - 1);
	int row = std::clamp(static_cast<int>(y * inv_cell_size_), 0, rows_ - 1);
	int idx = CellIndex(col, row);

	grid_[idx].push_back(id);
	entity_cell_[id] = idx;
	entity_x_[id] = x;
	entity_y_[id] = y;
}

void SpatialGrid::Update(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_GRID_UPDATE();
	auto it = entity_cell_.find(id);
	if (it == entity_cell_.end()) {
		Insert(id, x, y);
		return;
	}

	int col = std::clamp(static_cast<int>(x * inv_cell_size_), 0, cols_ - 1);
	int row = std::clamp(static_cast<int>(y * inv_cell_size_), 0, rows_ - 1);
	int new_idx = CellIndex(col, row);
	int old_idx = it->second;

	entity_x_[id] = x;
	entity_y_[id] = y;

	if (new_idx == old_idx) return;

	// Remove from old cell
	auto& old_cell = grid_[old_idx];
	old_cell.erase(std::remove(old_cell.begin(), old_cell.end(), id), old_cell.end());

	// Insert into new cell
	grid_[new_idx].push_back(id);
	entity_cell_[id] = new_idx;
}

void SpatialGrid::Remove(entity::EntityId id) {
	ENGINE_PROFILE_AOI_GRID_REMOVE();
	auto it = entity_cell_.find(id);
	if (it == entity_cell_.end()) return;

	int idx = it->second;
	auto& cell = grid_[idx];
	cell.erase(std::remove(cell.begin(), cell.end(), id), cell.end());

	entity_cell_.erase(it);
	entity_x_.erase(id);
	entity_y_.erase(id);
}

std::vector<entity::EntityId> SpatialGrid::QueryRadius(float x, float y, float radius) const {
	ENGINE_PROFILE_AOI_QUERY();
	int min_col, min_row, max_col, max_row;
	CellIndices(x, y, radius, min_col, min_row, max_col, max_row);

	std::vector<entity::EntityId> result;
	float r2 = radius * radius;

	for (int row = min_row; row <= max_row; ++row) {
		for (int col = min_col; col <= max_col; ++col) {
			for (auto id : grid_[CellIndex(col, row)]) {
				float ex, ey;
				GetPosition(id, ex, ey);
				float dx = ex - x;
				float dy = ey - y;
				if (dx * dx + dy * dy <= r2) {
					result.push_back(id);
				}
			}
		}
	}

	return result;
}

std::vector<entity::EntityId> SpatialGrid::QueryAOI(entity::EntityId id) const {
	ENGINE_PROFILE_SCOPE("engine.aoi", "GridQueryAOI");
	float x, y;
	GetPosition(id, x, y);
	return QueryAOIAt(x, y);
}

std::vector<entity::EntityId> SpatialGrid::QueryAOIAt(float x, float y) const {
	ENGINE_PROFILE_SCOPE("engine.aoi", "GridQueryAOIAt");
	int col = std::clamp(static_cast<int>(x * inv_cell_size_), 0, cols_ - 1);
	int row = std::clamp(static_cast<int>(y * inv_cell_size_), 0, rows_ - 1);

	std::vector<entity::EntityId> result;
	for (int dr = -1; dr <= 1; ++dr) {
		for (int dc = -1; dc <= 1; ++dc) {
			int nc = col + dc;
			int nr = row + dr;
			if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_) continue;
			const auto& cell = grid_[CellIndex(nc, nr)];
			result.insert(result.end(), cell.begin(), cell.end());
		}
	}
	return result;
}

void SpatialGrid::Clear() {
	for (auto& cell : grid_) {
		cell.clear();
	}
	entity_cell_.clear();
	entity_x_.clear();
	entity_y_.clear();
}

}  // namespace aoi
}  // namespace engine
