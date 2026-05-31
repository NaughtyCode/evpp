# AOI SpatialGrid 性能优化记录

> 日期: 2026-06-01  
> 范围: `src/runtime/aoi/`、`src/tests/performance/`、`src/tests/unit/aoi/`  
> 目标: 在确保 AOI 查询和可见性结果正确的前提下，降低游戏服务器高频移动、半径查询和可见性重算成本。

## 审查结论

本轮审查优先关注 `src/runtime` 中真实游戏服务器高频路径。AOI 系统是典型热路径:

- 玩家/实体移动会频繁调用 `AOIManager::OnEntityMove()`。
- `RecomputeVisibility()` 依赖 `SpatialGrid::QueryRadius()`。
- `SpatialGrid::Update()` 在跨 cell 移动时会从旧 cell 删除实体。

原实现中，`SpatialGrid` 使用:

- `grid_`: `std::vector<std::vector<EntityId>>`
- `entity_cell_`: `EntityId -> cell_index`
- `positions_`: `EntityId -> Position`

这导致两个主要性能问题:

1. `QueryRadius()` 对每个候选实体都要再查一次 `positions_` 哈希表，候选越多，cache miss 和 hash 开销越明显。
2. `Update()` / `Remove()` 从 cell 中删除实体时使用 `erase(std::remove(...))`，拥挤 cell 下是 O(cell_size)。

## 优化修改

### 1. 合并 cell 内实体和位置

文件:

- `src/runtime/aoi/spatial_index.h`
- `src/runtime/aoi/spatial_index.cc`

将 cell 从只存 `EntityId` 改为存:

```cpp
struct CellEntry {
    entity::EntityId id;
    Position position;
};
```

`QueryRadius()` 现在直接遍历 cell 内的 `{id, position}`，不再对每个候选实体访问 `positions_` 哈希表。

### 2. 引入实体到 cell 槽位的反向索引

将 `entity_cell_` 从 `EntityId -> int cell_index` 改为:

```cpp
struct CellRef {
    int cell_index;
    size_t entry_index;
};
```

这样 `Update()` 和 `Remove()` 能直接定位实体在 cell vector 中的位置。

### 3. 删除/跨 cell 移动改为 swap-remove

新增 `RemoveCellEntry()`:

- 将被删除槽位替换为 cell 末尾元素。
- 更新被移动实体的 `CellRef.entry_index`。
- `pop_back()` 完成删除。

拥挤 cell 下，跨 cell 移动和删除从 O(cell_size) 降为 O(1)。

### 4. 查询结果预留容量

`QueryRadius()` 和 `QueryAOIAt()` 先统计涉及 cell 的候选数量并 `reserve()`，降低结果 vector 扩容次数。

### 5. 性能测试入口修复和新增 benchmark

文件:

- `src/tests/performance/CMakeLists.txt`
- `src/tests/performance/bench_aoi.cpp`
- `src/tests/performance/bench_tcp_throughput.cpp`

修改:

- performance benchmark 统一链接 `benchmark::benchmark_main`，修复原性能目标缺少 `main` 的链接问题。
- 新增 `bench_aoi`，覆盖:
  - `BM_SpatialGrid_QueryRadius`
  - `BM_SpatialGrid_UpdateAcrossCrowdedCell`
  - `BM_AOIManager_MoveInCrowd`
- `bench_tcp_throughput.cpp` 补齐 `Buffer` 和 `TCPConn` include，修复已有 `bench_tcp` 编译问题。

## 正确性保护

新增单元测试:

- `SpatialGrid keeps moved cell references valid after remove`

覆盖场景:

1. 多个实体在同一 cell。
2. 删除中间实体触发 swap-remove。
3. 更新被 swap 到删除槽位的实体。
4. 确认旧位置、新位置、实体数量均正确。

同时执行 AOI 集成测试，确认上层实体 AOI 可见性行为不变。

## 测试环境

测试日期: 2026-06-01  
平台: Windows, MSVC, Visual Studio 18 2026 generator  
构建类型: `Release`  
临时性能构建目录: `artifacts/build-perf`  
性能构建开关:

```powershell
cmake -S src -B artifacts\build-perf `
  -DBUILD_TESTING=ON `
  -DCLOUDENGINE_BUILD_BENCHMARKS=ON `
  -DENGINE_MONGODB_ENABLED=OFF `
  -DENGINE_PHYSICS_ENABLED=OFF `
  -DENGINE_PROFILER_ENABLED=OFF
```

关闭 MongoDB、Physics、Profiler 是为了缩短性能验证构建时间；本次 benchmark 只依赖 AOI runtime 代码，优化前后使用同一组构建开关。

## 执行命令

构建目标:

```powershell
cmake --build artifacts\build-perf --config Release --target bench_aoi test_aoi test_integ_entity_aoi -- /m /nr:false
```

正确性测试:

```powershell
artifacts\bin\Release\test_aoi.exe
artifacts\bin\Release\test_integ_entity_aoi.exe
```

性能测试:

```powershell
artifacts\bin\Release\bench_aoi.exe `
  --benchmark_min_time=0.2s `
  --benchmark_repetitions=3 `
  --benchmark_report_aggregates_only=true `
  --benchmark_format=console
```

## 正确性结果

优化前:

| 测试 | 结果 |
|---|---|
| `test_aoi.exe` | 96 assertions / 44 test cases passed |

优化后:

| 测试 | 结果 |
|---|---|
| `test_aoi.exe` | 102 assertions / 45 test cases passed |
| `test_integ_entity_aoi.exe` | 38 assertions / 10 test cases passed |

额外构建验证:

| 目标 | 结果 |
|---|---|
| `bench_buffer` | build passed |
| `bench_timer` | build passed |
| `bench_msgpack` | build passed |
| `bench_scriptvm` | build passed |
| `bench_config` | build passed |
| `bench_tcp` | build passed |
| `bench_aoi` | build passed |

构建过程中仍有既有 MSVC runtime link warning:

```text
LNK4098: defaultlib 'LIBCMT' conflicts with use of other libs
```

该 warning 已存在于测试/benchmark 链接链路中，本次未改变 runtime library 配置。

## 性能对比

`bench_aoi.exe --benchmark_min_time=0.2s --benchmark_repetitions=3 --benchmark_report_aggregates_only=true`

| Benchmark mean time | 优化前 | 优化后 | 提升 |
|---|---:|---:|---:|
| `BM_SpatialGrid_QueryRadius/1000` | 427 ns | 157 ns | 2.72x |
| `BM_SpatialGrid_QueryRadius/10000` | 2303 ns | 362 ns | 6.36x |
| `BM_SpatialGrid_UpdateAcrossCrowdedCell/1000` | 47.1 ns | 6.75 ns | 6.98x |
| `BM_SpatialGrid_UpdateAcrossCrowdedCell/10000` | 272 ns | 6.91 ns | 39.36x |
| `BM_AOIManager_MoveInCrowd/1000` | 19779 ns | 15108 ns | 1.31x |

最终优化后原始摘要:

```text
BM_SpatialGrid_QueryRadius/1000_mean                       157 ns
BM_SpatialGrid_QueryRadius/10000_mean                      362 ns
BM_SpatialGrid_UpdateAcrossCrowdedCell/1000_mean          6.75 ns
BM_SpatialGrid_UpdateAcrossCrowdedCell/10000_mean         6.91 ns
BM_AOIManager_MoveInCrowd/1000_mean                      15108 ns
```

## 风险评估

行为风险:

- `SpatialGrid` public API 未变化。
- 查询结果顺序未被 API 承诺；swap-remove 可能改变同 cell 内部顺序，但现有测试和上层 `AOIManager::GetVisibleEntities()` 会排序输出。
- `QueryAOIAt()` 仍返回同 cell 和邻近 cell 所有实体。

性能风险:

- `CellEntry` 在每个 cell 中额外存储 position，换取移除全局 `positions_` 哈希表和查询时哈希访问。
- 半径查询增加了一次候选数量统计循环用于 `reserve()`；在测试场景中收益显著，主要来自避免哈希查找和减少 vector 扩容。

## 结论

本次优化把 AOI grid 的高频查询路径改为更 cache-friendly 的 cell-local 数据结构，并将拥挤 cell 下的更新/删除从线性扫描降为直接定位。测试数据表明:

- `SpatialGrid::QueryRadius()` 提升 2.7x 到 6.3x。
- 拥挤 cell 跨格移动提升 7x 到 39x。
- 上层 `AOIManager::OnEntityMove()` 在 1000 实体场景提升约 31%。

正确性测试和实体-AOI 集成测试均通过。
