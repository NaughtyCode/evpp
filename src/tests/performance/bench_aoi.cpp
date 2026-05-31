#include <benchmark/benchmark.h>

#include <memory>

#include <runtime/aoi/aoi_manager.h>
#include <runtime/aoi/spatial_index.h>
#include <runtime/entity/entity_id.h>

namespace {

using engine::aoi::AOIManager;
using engine::aoi::SpatialGrid;
using engine::entity::EntityId;

constexpr float kWorldSize = 1000.0f;
constexpr float kCellSize = 20.0f;

void PopulateUniform(SpatialGrid& grid, int count) {
	for (int i = 0; i < count; ++i) {
		const int x_bucket = (i * 37) % 997;
		const int y_bucket = (i * 73) % 991;
		grid.Insert(static_cast<EntityId>(i + 1),
					static_cast<float>(x_bucket),
					static_cast<float>(y_bucket));
	}
}

void PopulateCrowdedCell(SpatialGrid& grid, int count) {
	for (int i = 0; i < count; ++i) {
		const float offset = static_cast<float>(i % 16) * 0.5f;
		grid.Insert(static_cast<EntityId>(i + 1), 5.0f + offset, 5.0f + offset);
	}
}

}  // namespace

static void BM_SpatialGrid_QueryRadius(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	SpatialGrid grid(kWorldSize, kWorldSize, kCellSize);
	PopulateUniform(grid, count);

	for (auto _ : state) {
		auto result = grid.QueryRadius(500.0f, 500.0f, 80.0f);
		benchmark::DoNotOptimize(result.data());
		benchmark::DoNotOptimize(result.size());
	}

	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_SpatialGrid_QueryRadius)->Arg(1000)->Arg(10000);

static void BM_SpatialGrid_UpdateAcrossCrowdedCell(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	SpatialGrid grid(kWorldSize, kWorldSize, kCellSize);
	PopulateCrowdedCell(grid, count);

	bool near_origin = true;
	for (auto _ : state) {
		if (near_origin) {
			grid.Update(1, 500.0f, 500.0f);
		} else {
			grid.Update(1, 5.0f, 5.0f);
		}
		near_origin = !near_origin;
		benchmark::ClobberMemory();
	}
}
BENCHMARK(BM_SpatialGrid_UpdateAcrossCrowdedCell)->Arg(1000)->Arg(10000);

static void BM_AOIManager_MoveInCrowd(benchmark::State& state) {
	const int count = static_cast<int>(state.range(0));
	auto grid = std::make_unique<SpatialGrid>(kWorldSize, kWorldSize, kCellSize);
	AOIManager manager(std::move(grid));

	for (int i = 0; i < count; ++i) {
		const auto id = static_cast<EntityId>(i + 1);
		manager.RegisterEntity(id, 50.0f);
		const float x = static_cast<float>((i * 37) % 997);
		const float y = static_cast<float>((i * 73) % 991);
		manager.OnEntityMove(id, x, y);
	}

	int step = 0;
	for (auto _ : state) {
		const float x = 450.0f + static_cast<float>((step * 17) % 100);
		const float y = 450.0f + static_cast<float>((step * 29) % 100);
		manager.OnEntityMove(1, x, y);
		++step;
		benchmark::ClobberMemory();
	}
}
BENCHMARK(BM_AOIManager_MoveInCrowd)->Arg(1000);
