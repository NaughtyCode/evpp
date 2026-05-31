#include "runtime/aoi/spatial_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace aoi {

namespace {

constexpr size_t kMaxGridCellCount = 10'000'000;

bool IsFinite(float value) {
	return std::isfinite(value);
}

float ValidateCellSize(float cell_size) {
	if (!IsFinite(cell_size) || cell_size <= 0.0f) {
		throw std::invalid_argument("SpatialGrid cell size must be finite and positive");
	}
	return cell_size;
}

int ComputeAxisCells(float world_extent, float cell_size) {
	if (!IsFinite(world_extent) || world_extent <= 0.0f) {
		throw std::invalid_argument("SpatialGrid world dimensions must be finite and positive");
	}

	const double cells = std::ceil(static_cast<double>(world_extent) /
								   static_cast<double>(cell_size));
	if (cells > static_cast<double>(std::numeric_limits<int>::max())) {
		throw std::length_error("SpatialGrid axis cell count exceeds int range");
	}
	return std::max(1, static_cast<int>(cells));
}

void ValidatePosition(float x, float y) {
	if (!IsFinite(x) || !IsFinite(y)) {
		throw std::invalid_argument("SpatialGrid positions must be finite");
	}
}

}  // namespace

SpatialGrid::SpatialGrid(float world_width, float world_height, float cell_size)
	: cell_size_(ValidateCellSize(cell_size))
	, inv_cell_size_(1.0f / cell_size_)
	, world_width_(world_width)
	, world_height_(world_height) {
	cols_ = ComputeAxisCells(world_width, cell_size);
	rows_ = ComputeAxisCells(world_height, cell_size);

	const size_t cell_count = static_cast<size_t>(cols_) * static_cast<size_t>(rows_);
	if (cell_count > kMaxGridCellCount) {
		throw std::length_error("SpatialGrid cell count exceeds safety limit");
	}
	grid_.resize(cell_count);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "SpatialGrid: [{}]x[{}] cells, cell_size=[{}]",
					cols_, rows_, cell_size_);
}

int SpatialGrid::CellIndex(int col, int row) const {
	return row * cols_ + col;
}

int SpatialGrid::CellIndexForPosition(float x, float y) const {
	const auto clamp_axis = [this](float value, int limit) {
		const double scaled = std::floor(static_cast<double>(value) *
										 static_cast<double>(inv_cell_size_));
		if (scaled <= 0.0) return 0;
		if (scaled >= static_cast<double>(limit - 1)) return limit - 1;
		return static_cast<int>(scaled);
	};

	const int col = clamp_axis(x, cols_);
	const int row = clamp_axis(y, rows_);
	return CellIndex(col, row);
}

void SpatialGrid::CellIndices(float x, float y, float radius,
							   int& min_col, int& min_row,
							   int& max_col, int& max_row) const {
	const auto clamp_axis = [this](double value, int limit) {
		const double scaled = std::floor(value * static_cast<double>(inv_cell_size_));
		if (scaled <= 0.0) return 0;
		if (scaled >= static_cast<double>(limit - 1)) return limit - 1;
		return static_cast<int>(scaled);
	};

	min_col = clamp_axis(static_cast<double>(x) - static_cast<double>(radius), cols_);
	min_row = clamp_axis(static_cast<double>(y) - static_cast<double>(radius), rows_);
	max_col = clamp_axis(static_cast<double>(x) + static_cast<double>(radius), cols_);
	max_row = clamp_axis(static_cast<double>(y) + static_cast<double>(radius), rows_);
}

bool SpatialGrid::TryGetPosition(entity::EntityId id, Position& position) const {
	auto it = entity_cell_.find(id);
	if (it == entity_cell_.end()) return false;
	const CellRef& ref = it->second;
	const auto& cell = grid_[ref.cell_index];
	if (ref.entry_index >= cell.size()) return false;
	position = cell[ref.entry_index].position;
	return true;
}

void SpatialGrid::RemoveCellEntry(const CellRef& ref) {
	auto& cell = grid_[ref.cell_index];
	if (ref.entry_index >= cell.size()) return;

	const size_t last_index = cell.size() - 1;
	if (ref.entry_index != last_index) {
		cell[ref.entry_index] = cell[last_index];
		auto moved_it = entity_cell_.find(cell[ref.entry_index].id);
		if (moved_it != entity_cell_.end()) {
			moved_it->second = CellRef{ref.cell_index, ref.entry_index};
		}
	}
	cell.pop_back();
}

void SpatialGrid::Insert(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_GRID_INSERT();
	ValidatePosition(x, y);

	if (entity_cell_.find(id) != entity_cell_.end()) {
		Update(id, x, y);
		return;
	}

	int idx = CellIndexForPosition(x, y);

	auto& cell = grid_[idx];
	entity_cell_[id] = CellRef{idx, cell.size()};
	cell.push_back(CellEntry{id, Position{x, y}});
}

void SpatialGrid::Update(entity::EntityId id, float x, float y) {
	ENGINE_PROFILE_AOI_GRID_UPDATE();
	ValidatePosition(x, y);

	auto it = entity_cell_.find(id);
	if (it == entity_cell_.end()) {
		Insert(id, x, y);
		return;
	}

	int new_idx = CellIndexForPosition(x, y);
	const CellRef old_ref = it->second;
	int old_idx = old_ref.cell_index;

	if (new_idx == old_idx) {
		auto& cell = grid_[old_ref.cell_index];
		if (old_ref.entry_index < cell.size()) {
			cell[old_ref.entry_index].position = Position{x, y};
		}
		return;
	}

	RemoveCellEntry(old_ref);

	auto& new_cell = grid_[new_idx];
	it->second = CellRef{new_idx, new_cell.size()};
	new_cell.push_back(CellEntry{id, Position{x, y}});
}

void SpatialGrid::Remove(entity::EntityId id) {
	ENGINE_PROFILE_AOI_GRID_REMOVE();
	auto it = entity_cell_.find(id);
	if (it == entity_cell_.end()) return;

	RemoveCellEntry(it->second);
	entity_cell_.erase(it);
}

std::vector<entity::EntityId> SpatialGrid::QueryRadius(float x, float y, float radius) const {
	ENGINE_PROFILE_AOI_QUERY();
	if (!IsFinite(x) || !IsFinite(y) || !IsFinite(radius) || radius < 0.0f) {
		return {};
	}

	int min_col, min_row, max_col, max_row;
	CellIndices(x, y, radius, min_col, min_row, max_col, max_row);

	size_t candidate_count = 0;
	for (int row = min_row; row <= max_row; ++row) {
		for (int col = min_col; col <= max_col; ++col) {
			candidate_count += grid_[CellIndex(col, row)].size();
		}
	}

	std::vector<entity::EntityId> result;
	result.reserve(candidate_count);
	float r2 = radius * radius;

	for (int row = min_row; row <= max_row; ++row) {
		for (int col = min_col; col <= max_col; ++col) {
			for (const auto& entry : grid_[CellIndex(col, row)]) {
				float dx = entry.position.x - x;
				float dy = entry.position.y - y;
				if (dx * dx + dy * dy <= r2) {
					result.push_back(entry.id);
				}
			}
		}
	}

	return result;
}

std::vector<entity::EntityId> SpatialGrid::QueryAOI(entity::EntityId id) const {
	ENGINE_PROFILE_SCOPE("engine.aoi", "GridQueryAOI");
	Position position;
	if (!TryGetPosition(id, position)) return {};
	return QueryAOIAt(position.x, position.y);
}

std::vector<entity::EntityId> SpatialGrid::QueryAOIAt(float x, float y) const {
	ENGINE_PROFILE_SCOPE("engine.aoi", "GridQueryAOIAt");
	if (!IsFinite(x) || !IsFinite(y)) return {};

	int idx = CellIndexForPosition(x, y);
	int col = idx % cols_;
	int row = idx / cols_;

	size_t candidate_count = 0;
	for (int dr = -1; dr <= 1; ++dr) {
		for (int dc = -1; dc <= 1; ++dc) {
			int nc = col + dc;
			int nr = row + dr;
			if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_) continue;
			candidate_count += grid_[CellIndex(nc, nr)].size();
		}
	}

	std::vector<entity::EntityId> result;
	result.reserve(candidate_count);
	for (int dr = -1; dr <= 1; ++dr) {
		for (int dc = -1; dc <= 1; ++dc) {
			int nc = col + dc;
			int nr = row + dr;
			if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_) continue;
			const auto& cell = grid_[CellIndex(nc, nr)];
			for (const auto& entry : cell) {
				result.push_back(entry.id);
			}
		}
	}
	return result;
}

void SpatialGrid::Clear() {
	for (auto& cell : grid_) {
		cell.clear();
	}
	entity_cell_.clear();
}

}  // namespace aoi
}  // namespace engine
