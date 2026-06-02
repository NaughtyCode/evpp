# MMO AOI 技术深度分析与 evpp3 落地路线

更新日期：2026-06-02

AOI（Area of Interest，兴趣区域）不是一个单独算法，而是 MMO 服务器用来回答“某个连接此刻应该接收哪些对象、哪些属性、以什么频率接收”的完整工程系统。公开资料能确认的共同趋势是：现代 MMO 很少只靠“九宫格”；生产架构通常由空间索引、逻辑可见性、复制调度、带宽预算、分区/副本和过载保护共同组成。

本文基于公开的引擎文档、商业中间件文档、BigWorld 式 MMO 服务器资料、EVE Online 技术博客、云游戏服务器编排资料、网络同步工程文章、AOI/Interest Management 学术综述，以及本仓库当前 `src/runtime/aoi`、Lua/C API 包装、API 文档和测试实现，给出面向 evpp3 的技术判断和路线。

## 0. 本轮审查方式

本次按六个视角做多轮审查，并把修正直接合入正文：

1. 资料准确性：校验公开资料是否仍可访问，去掉过度推断，给过时或特定版本资料加限定。
2. MMO 完整性：补齐空间 AOI、逻辑可见性、复制调度、分区、热点保护、反作弊和运维编排之间的边界。
3. 当前代码一致性：对照 `SpatialGrid`、`AOIManager`、Lua 绑定、C Client API、API 文档和测试，明确当前实现的真实语义和边界。
4. 工程落地性：把抽象建议收敛为 P0 到 P4 的接口、数据结构、测试和监控路线。
5. 文档可维护性：补充术语、风险清单、资料限制和参考链接，避免读者把“行业趋势”误解为当前已实现能力。
6. 契约可验证性：把初始化替换、查询顺序、回调重入、链接重定向和测试缺口写成可回归的验收项。

## 1. 结论先行

今天的 MMO AOI 主流结论如下：

1. 单区服、单地图、副本、野外小规模场景，首选均匀网格或空间哈希。它实现简单、缓存友好、查询成本稳定，是商业项目中最常见的基础层。
2. 大世界不会只靠 AOI 解决扩容。它会先做地图分区、地图副本、频道、相位、战场实例或 Cell Server 分治，再在每个分区内部跑 AOI。
3. 热点战斗是 AOI 的硬上限。所有人都在同一个兴趣区域内时，任何空间裁剪都会退化，必须配合人数上限、状态降级、更新优先级、低频 LOD、技能表现简化、战场实例或 EVE 式时间膨胀。
4. 现代 AOI 的核心产物不是“附近实体列表”，而是每个连接的复制列表。Unreal Replication Graph、Photon Interest Key、Unity/Mirror 可见性系统都在把 AOI 从单纯距离查询提升为复制策略。
5. 安全性和玩法规则是 AOI 的一部分。隐身、阵营、队伍、相位、视线遮挡、匹配房间、任务阶段都必须参与过滤，否则客户端会收到不该知道的状态。
6. evpp3 当前实现适合作为 Zone 内的基础 AOI：固定二维网格、精确半径查询、方向性可见集合、Lua enter/leave 回调和 C Client 查询包装。它距离生产 MMO AOI 还缺少空间/层过滤、逻辑谓词、批处理调度、热点降级和分布式边界代理。

## 2. 现代 MMO AOI 的分层模型

生产系统通常按下面的流水线工作：

```text
连接/玩家
  -> 世界分区：Region、Zone、Map Instance、Cell Server、Shard、Channel
  -> 空间候选：Grid、Spatial Hash、Quadtree、BVH、Portal/PVS
  -> 逻辑过滤：阵营、队伍、相位、隐身、任务、房间、对象类型、权限
  -> 复制调度：可靠 enter/leave、状态快照、增量属性、移动包、RPC、优先级
  -> 预算控制：每连接字节预算、每 tick CPU 预算、距离 LOD、最大同屏数
  -> 客户端生命周期：spawn、despawn、hide、show、late join 同步
```

这个模型解释了为什么“空间 AOI 算法正确”仍然可能不够：如果没有复制调度，热点区会把带宽打满；如果没有逻辑过滤，客户端会收到隐身单位；如果没有分区，主城或大规模战斗会把单进程压垮。

## 3. 公开资料中的技术信号

| 系统/资料 | 可确认的技术点 | 对 MMO AOI 的启发 |
| --- | --- | --- |
| ACM/IBM Interest Management 综述 | Interest Management 是分布式虚拟环境和 MMOG 的核心扩展手段，需要在不同方案之间权衡延迟、带宽、准确性和成本。 | AOI 应被视为系统工程，不是单一数据结构。 |
| Springer AOI in MMOG 条目 | AOI 是玩家感兴趣的虚拟世界部分，既可用于中心化 C/S 降低消息量，也可用于 P2P/分布式架构。 | 中心化 MMO 仍然需要 AOI，P2P 方案学术上丰富但商业 MMO 受反作弊限制。 |
| Unreal Replication Graph | 用持久节点为每个连接构建复制列表，避免每个 Actor 对每个连接做逐一判断。Fortnite 级别的 Actor 数量需要这种复制图。 | 大型在线游戏需要“按角色/状态/空间预分组”的复制层。 |
| Unity Netcode NetworkObject Visibility | 可见对象会在客户端保持 spawned clone；隐藏对象会被 despawn/destroy，并停止网络流量；旧 `docs-multiplayer` 概念页已转向 package 文档，应按项目锁定包版本核对 API。 | AOI 直接驱动客户端对象生命周期，不只是减少移动包。 |
| Photon Fusion Interest Management | 使用 interest key 和 spatial hash AOI；全局对象绕过过滤，小房间可不启用 AOI。 | Key/频道式订阅适合把空间和玩法规则统一编码。 |
| Mirror Interest Management | 内置 Spatial Hashing、Distance、Scene、Team、Match 等多种过滤器。 | 生产 AOI 常是“空间过滤 + 语义过滤”的组合。 |
| BigWorld Server | CellApp 管理空间 Cell，边界附近创建 ghost，客户端实体按 AoI 构建更新包，并维护优先队列。 | 无缝世界需要 Cell/Ghost/Handoff，而不是把世界交给一个 AOI 网格。 |
| EVE Online Time Dilation | 当单节点过载时减慢模拟时间，让任务队列保持可控。 | 当所有玩家挤进同一个 AOI 时，只能通过降级、限流或改变时间尺度保护公平性。 |
| Guild Wars 2 Megaserver | 按区域和启发式把玩家放入地图实例，地图满时创建新实例。 | Megaserver/实例化是控制单 AOI 热点的运营和架构手段。 |
| Agones/Kubernetes 与 Amazon GameLift | 负责专用服务器进程的部署、伸缩、分配、匹配或会话放置。 | 编排和托管能扩容进程数，但不会替代进程内 AOI，也不能消除单热点的交互密度。 |
| State Synchronization / Snapshot Replication 资料 | 网络同步需要优先级、带宽预算、最新值覆盖、量化和压缩。 | AOI 后面必须接复制调度，否则候选集正确也可能把带宽打满。 |

## 4. AOI 的核心目标和成本模型

没有 AOI 时，如果 `N` 个实体都需要相互同步，广播关系接近 `O(N^2)`。即使每个实体每秒只发很小的移动包，热点区也会迅速触发 CPU、带宽、序列化和客户端渲染瓶颈。

AOI 的核心目标：

- 降低服务器发送量：只向连接发送相关实体和相关属性。
- 降低服务器计算量：避免每 tick 做全局 `entity x connection` 判断。
- 降低客户端负载：未进入 AOI 的对象不创建、不更新、不渲染。
- 降低作弊面：客户端不接收不该知道的隐藏状态。
- 保持体验连续：enter/leave 不抖动，边界和瞬移不丢事件。

均匀网格的粗略成本：

```text
候选实体数 = 查询覆盖格子数 * 格内平均实体数
格内平均实体数 ≈ 单位面积密度 ρ * C^2
查询覆盖格子数 ≈ (2 * ceil(R / C) + 1)^2
```

其中 `R` 是兴趣半径，`C` 是格子边长。`C` 太大时格内实体过多，距离过滤成本高；`C` 太小时格子访问和跨格维护成本高。精确半径查询可以让 `C` 比 `R` 小一些；如果只用固定 9 宫格查询，则必须保证格子尺寸和可视半径的关系不漏查。

## 5. AOI 算法谱系

### 5.1 全局广播

适用场景：4 人合作、小房间、大厅状态、少量全局单例对象。

优点是简单可靠，缺点是没有扩展性。Photon 文档也明确指出，小型房间或必须全员可见的 GameState 适合全局兴趣。MMO 主世界不能依赖它，但它仍适合公告、天气、全局 Boss 阶段、服务器时间等少量状态。

### 5.2 距离暴力检测

适用场景：几十到一两百对象的副本、测试、原型。

做法是每个 observer 遍历所有对象，按距离过滤。实现成本最低，但 `O(N^2)` 随人数平方增长，不适合持续在线的大区。

### 5.3 均匀网格与空间哈希

适用场景：大多数 2D/2.5D MMORPG 野外、城镇、副本、开放地图。

做法：

- 把世界划分为固定尺寸格子。
- 实体按位置插入格子。
- 查询时只扫描半径覆盖的格子，再做精确距离过滤。

主要变体：

- 固定二维数组：适合尺寸固定、地图不大的场景。访问快，内存可预估。
- 稀疏哈希网格：适合超大地图或空旷世界，只为有实体的格子分配内存。
- 3D 网格：适合飞行、太空、体素、垂直楼层真实参与交互的场景。
- Paged grid：大地图按 page 分配小数组，兼顾稀疏和缓存局部性。

evpp3 当前 `SpatialGrid` 使用固定二维数组，适合“一个有限地图/副本内 AOI”。如果后续目标是无缝大世界，应新增稀疏或分页网格，而不是把固定数组无限扩大。

### 5.4 分层网格和热点细分

适用场景：同一地图既有稀疏野外，也有密集主城或集结点。

做法：

- 常规区域用粗网格。
- 某格实体数超过阈值后，局部启用二级小网格。
- 不同半径对象进入不同层，例如玩家、怪物、远景单位、巨型 Boss 分开索引。

分层网格比四叉树更适合高频移动实体，因为局部分桶更新简单，内存可控。它的难点是阈值、迁移和查询合并。

### 5.5 四叉树、八叉树、BVH、R-tree

适用场景：密度差异极大、查询区域形状复杂、静态对象多、地图结构复杂。

优点是可以自适应空间密度。缺点是移动实体频繁重插、树结构维护复杂、缓存局部性不稳定。对 MMORPG 中高频移动的玩家实体，四叉树通常不是第一选择；对静态物件、建筑、触发器、区域多边形、导航辅助查询则很有价值。

### 5.6 Sweep、十字链表和排序轴

适用场景：2D 空间、半径变化频繁、实体数中等、需要精确邻近关系。

它通过按 X/Y 轴维护有序链表或数组，移动时局部交换，查询时查找轴向范围交集。理论上优雅，但在热点、重叠坐标和大量瞬移时维护成本高。工程上通常不如网格稳定。

### 5.7 Portal、PVS、视线和遮挡

适用场景：室内 MMO、地牢、建筑多、竞技/潜行玩法、防作弊要求高。

距离可见不等于网络可见。墙后敌人、隐身单位、不同楼层、房间门关闭时都不应下发完整状态。常见做法：

- 粗 AOI 用网格找候选。
- 再用房间/portal/PVS/LOS 做二次过滤。
- 对可能作弊敏感的信息，只下发低精度或延迟状态。

这类过滤通常成本较高，应只对玩家连接和关键对象执行，不应对所有 NPC 互相执行。

### 5.8 Interest Group、频道和 Key 订阅

适用场景：地图块、房间、战场、队伍、频道、相位、分布式路由。

Photon 的 interest key 思路、Photon Server 的 Interest Groups、以及许多自研网关，本质都是把可见性编码为频道订阅。对象发布到某些 key，连接订阅某些 key，路由层只转发 key 匹配的更新。

优点：

- 与网络层天然匹配。
- 可以把空间格子、队伍、场景、相位统一成订阅维度。
- 便于分布式路由和跨进程边界同步。

缺点：

- key 粒度过粗会多发。
- key 粒度过细会造成订阅频繁变更。
- 多维规则组合需要谨慎设计，避免遗漏或重复。

### 5.9 Replication Graph 和复制节点

适用场景：大型多人动作游戏、Battle Royale、Actor 数量远大于玩家数的场景。

Unreal Replication Graph 的重要启发是：不要让每个 Actor 每帧对每个连接回答“我是否相关”。更好的方式是把 Actor 按空间、类型、状态、生命周期放进持久节点，由节点缓存和构建连接复制列表。

MMO 自研系统可以采用类似结构：

```text
ReplicationRoot
  SpatialNode(space_id, grid)
  AlwaysRelevantNode(world state, weather, match clock)
  OwnerNode(inventory, private quest state)
  TeamNode(party, guild, raid)
  DormancyNode(static objects, doors, resource nodes)
  CombatPriorityNode(damage source, target, projectiles)
```

AOI 只负责给出候选集合；最终是否发送、发送哪些属性、以多高频率发送，交给复制图或复制调度器。

### 5.10 逻辑过滤

现代 MMO 的实际可见性一般是：

```text
visible(observer, target) =
    same_space(observer, target)
 && same_layer_or_phase(observer, target)
 && spatially_close(observer, target)
 && category_allowed(observer, target)
 && gameplay_rule_allowed(observer, target)
 && security_rule_allowed(observer, target)
```

常见逻辑维度：

- `space_id`：地图、副本、战场、房间。
- `layer_id`：频道、分线、地图实例。
- `phase_id`：任务阶段、剧情相位、动态世界状态。
- 阵营/队伍/公会/团队。
- 隐身、潜行、伪装、观察者权限。
- 对象类型：玩家、NPC、掉落物、投射物、技能效果、环境对象。
- 私有状态：背包、任务目标、私有掉落，只能 owner 可见。

如果这些规则不进入 AOI，网络层就会把不该知道的状态发给客户端。

### 5.11 Aura、Nimbus 与方向性可见

学术 AOI/Interest Management 文献常用 `aura` 和 `nimbus` 区分“我能被多远感知”和“我能感知多远”：

- `nimbus`：observer 的感知范围。
- `aura`：target 的可被感知范围。
- 当只使用一个相同圆形半径时，关系通常表现为互相可见。
- 当 observer 的 nimbus 和 target 的 aura 分离时，可见关系天然是方向性的。

evpp3 当前实现更接近“observer nimbus + target 点对象”：`aoi_radius` 只决定 observer 能看多远，target 没有独立 aura。因此 `A` 能看到 `B` 不代表 `B` 能看到 `A`。如果后续需要大型 Boss、远景建筑、音效源、队伍标记这类“自身可被更远感知”的对象，应在 `AOIProfile` 中增加 target-side `aura_radius`，而不是把所有 observer 半径一起放大。

## 6. Cell-based AOI 的工程细节

Cell-based AOI 仍然是 MMO 最实用的基础层。它的核心是用空间局部性把全局遍历变成局部候选扫描。

### 6.1 数据结构

典型结构：

```cpp
struct EntityAOI {
    EntityId id;
    float x;
    float y;
    float radius;
    CellCoord cell;
    std::unordered_set<EntityId> visible;   // 我当前能看到谁
};

struct Cell {
    std::vector<EntityEntry> entries;
};
```

生产实现通常使用 `vector + swap-remove + entity -> cell/index`，避免链表的内存碎片和缓存 miss。evpp3 当前 `SpatialGrid` 正是这种设计：每个实体记录 `CellRef{cell_index, entry_index}`，移动跨格时用尾元素覆盖删除。

### 6.2 Spawn

实体首次进入：

1. 校验位置、半径、空间。
2. 插入空间索引。
3. 查询自身半径内候选。
4. 对每个候选执行逻辑过滤。
5. 生成 `Enter(observer, target)` 事件。
6. 发送目标完整初始快照，而不是只发移动增量。

如果系统采用方向性可见性，`A` 看到 `B` 不代表 `B` 看到 `A`。如果设计要求对称视野，应统一半径或显式建立双向关系。

### 6.3 Move

移动是 AOI 的热点路径。正确做法是基于新位置重新构建该 observer 的新可见集合，并与旧集合 diff：

```cpp
new_visible = QueryRadius(new_position, radius)
new_visible = FilterByRules(observer, new_visible)

entered = new_visible - old_visible
left    = old_visible - new_visible
stayed  = new_visible & old_visible

old_visible = new_visible
```

只比较新旧格子差集是不够的，因为共同格子中的实体也可能因距离变化进入或离开边缘。

对方向性 AOI，还要处理“移动者影响了哪些其他 observer”。evpp3 当前 `AOIManager::OnEntityMove` 的策略是：

- 移动者自己一定受影响。
- 旧位置附近、当前位置附近、最大 AOI 半径内的 observer 也可能受影响。
- 对受影响 observer 排序后逐一 `RecomputeVisibility`。

这是合理的单进程实现，成本取决于 `max_aoi_radius_` 覆盖范围内的候选数量。后续如果半径差异很大，应改成按 AOI profile 分层查询，避免一个超大半径实体拖高所有移动成本。

### 6.4 Despawn

实体离开：

- 对自己可见集合中的目标发送 `Leave(self, target)`。
- 对所有曾经看到自己的 observer 发送 `Leave(observer, self)`。
- 从空间索引和可见表彻底移除。

为了避免 `O(N)` 扫描全部 visible 集合，生产系统通常维护反向 watchers：

```text
visible[observer] = {target...}
watchers[target] = {observer...}
```

evpp3 当前 `AOIManager` 已维护 `watchers_` 反向索引。`RecomputeVisibility` 在 enter/leave diff 时同步维护 `visible_[observer]` 和 `watchers_[target]`，`UnregisterEntity` 使用反向索引找到曾经看见被删除实体的 observers，避免删除路径全量扫描所有 visible 集合。

### 6.5 进入/离开抖动

边界抖动会导致 enter/leave 高频切换。常见修复：

```text
进入半径 R_enter = R
离开半径 R_leave = R + hysteresis
```

这会让实体进入后必须离得更远才离开，减少网络事件和客户端对象闪烁。

### 6.6 格子尺寸选择

如果使用精确 `QueryRadius`，格子尺寸不是必须等于可视半径。更实用的选择方式：

- 目标格内实体数：普通区域 8 到 32，热点区域不要超过 64。
- `C` 可取常见玩家视野半径的 `0.5R` 到 `1.0R`。
- 如果大量实体半径差异大，按 profile 分多层网格，而不是用最大半径决定全局 `C`。
- 如果使用固定 9 宫格 `QueryAOI`，则必须证明 `C` 与最大查询半径不会漏查；否则用半径覆盖格子扫描。

evpp3 的 `QueryRadius` 会按半径覆盖格子，不依赖 9 宫格，因此更安全。`QueryAOI` 是 9 格便捷接口，应仅用于“同格 + 邻格候选”或最大业务半径不超过一个 `cell_size` 的受控场景，不能把它当成任意半径查询 API。

### 6.7 当前 evpp3 实现的边界语义

对照当前代码，需要明确以下行为：

- `SpatialGrid` 是固定世界数组，不是稀疏哈希网格。构造时会按 `world_width / cell_size` 和 `world_height / cell_size` 分配全部格子，并有 `10,000,000` 格安全上限。
- 坐标会被 clamp 到边界格。负坐标或超过世界尺寸的坐标不会被拒绝，而是落入最近边界格。这适合“世界边界内移动被上层约束”的场景，不适合表达“实体暂时不在地图内”。
- clamp 只影响实体所在格子和查询覆盖格子，不会改写实体保存的原始坐标。`QueryRadius` 的精确距离仍使用原始 `x/y`，所以越界实体可能存放在边界格里，但距离判断仍按越界坐标计算。
- `QueryRadius(x, y, radius)` 返回点半径内的所有实体；它是通用查询，不知道“调用者是谁”，因此不会自动排除调用者自身。
- `GetVisibleEntities(id)` 返回 `visible_[id]`，由 `RecomputeVisibility` 维护，会排除自身，并按 entity id 排序返回。
- `QueryRadius`、`QueryAOI` 和 `QueryAOIAt` 不排序。结果顺序来自格子扫描和格内 `vector` 顺序，`swap-remove` 删除也会改变格内顺序；不能把它当网络协议顺序。
- `aoi_radius = 0` 是合法半径，只能看到与 observer 同坐标的其他实体。
- `register_entity` 对同一个 id 调用时实际是 upsert：Lua/C API 会通过 `AOIManager::UpsertEntity` 原子更新半径和位置，避免先在旧位置产生一轮临时 enter/leave。普通移动仍应使用 `update_entity` / `game_aoi_move_entity`；只改半径应使用 `aoi.update_radius` / `game_aoi_update_radius`。
- `aoi.init` 在参数校验后会先构造新的 `SpatialGrid` 和 `AOIManager`；构造成功后才清理旧实例、旧回调和旧实体并替换为新实例。若构造失败并返回 `nil, err`，旧 AOI 状态仍保留。`aoi.shutdown` 会销毁 AOI 实例并清空回调。未初始化时，`get_visible` 和 `query_radius` 返回空表，`count` 返回 0。
- Lua 绑定的无效参数通过 `luaL_argerror` 抛出 Lua 参数错误；`nil, err` 只用于未初始化、回调重入 guard 和捕获到底层异常的 mutation API。C++ API 使用异常或空结果，C Client API 使用 `game_error_t`，三层错误语义不能混写。
- AOI 回调是同步调用。Lua 回调期间禁止再次调用 `register_entity`、`update_entity`、`unregister_entity`、`shutdown` 或 `set_event_callback` 修改 AOI，否则 Lua 绑定会返回 `nil, err`。
- C++ `AOIManager::SetEventCallback` 现在有统一回调重入 guard。回调派发期间同一个 manager 的 mutation API 会抛出 `std::logic_error`；Lua 绑定会在进入 manager 前返回 `nil, err`。
- `AOIManager` 和 `SpatialGrid` 没有内部锁，当前应按单线程或明确阶段所有权串行调用。若网络线程、逻辑线程、脚本线程都会触碰 AOI，需要先建立调度队列或 actor ownership。
- `OnEntityMove` 会按 id 排序受影响 observer，但 `RecomputeVisibility` 内部用 `unordered_set` 计算目标差集，同一 observer 的多个 enter/leave 目标派发顺序不是稳定契约。若上层复制系统要求确定性，应在派发前按 target id 排序。
- 当前没有 `space_id`、`layer_id`、`phase_id`、category、team、owner、visibility predicate。所有注册实体默认处于同一个逻辑世界。

本轮已把这些语义同步到 `resources/api/aoi/api.md` 和 `src/client/client.h` 的 AOI 注释中。后续如果运行时代码新增分区、对称可见或逻辑过滤能力，应同步更新这两处契约文档和本文件的实现评估。

### 6.8 半径、空间和规则变化

生产 AOI 不能只处理 move，还要处理以下变化：

- 半径变化：观察者能看到的目标集合变化，应重算该 observer 的 visible。
- 空间变化：实体从一个 `space/layer/phase` 切到另一个时，应先从旧空间 despawn，再在新空间 spawn，避免跨副本可见。
- 类型变化：隐身、阵营、队伍、owner-only、任务相位变化会改变逻辑过滤结果，需要重算相关 observer。
- 大对象变化：巨型 Boss、世界建筑、远景目标可能有独立的 `aura_radius` 或 `replication_radius`，不能简单套普通玩家视野。

evpp3 当前只直接支持位置和半径；空间、规则和大对象半径需要在下一阶段扩展。

## 7. 复制调度：AOI 之后真正发什么

AOI 输出候选集合后，还需要决定发送内容和频率。

推荐把实体更新分为几类：

| 类型 | 可靠性 | 频率 | 示例 |
| --- | --- | --- | --- |
| 生命周期 | 可靠、有序 | 变化时 | spawn、despawn、enter、leave |
| 战斗关键 | 可靠或高优先 | 即时或高频 | 伤害、技能命中、死亡、控制 |
| 移动状态 | 可丢弃、最新值覆盖旧值 | 5 到 20 Hz | 位置、朝向、速度 |
| 外观/社交 | 可靠但低频 | 变化时 | 装备外观、称号、表情 |
| 远距实体 | 低频、聚合 | 1 到 3 Hz | 远处玩家、背景 NPC |
| 静态对象 | Dormant | 变化时 | 门、采集物、场景机关 |

每个连接应有发送预算：

```text
connection_budget_bytes_per_tick
connection_budget_entities_per_tick
critical_queue
normal_queue
low_priority_queue
```

当预算不足时，不应随机丢包，而应按优先级降级：

1. 先保证自己、目标、攻击者、被攻击者、队友、附近危险物。
2. 再发送近距离玩家和 NPC。
3. 最后发送远距离外观、非关键移动、环境装饰。
4. 超出最大同屏数量时，按距离、威胁度、社交关系、目标锁定决定保留集合。

### 7.1 生命周期事件顺序

AOI 事件要和网络复制严格排序：

```text
Enter:
  1. 可靠发送 spawn/full snapshot
  2. 标记客户端对象已存在
  3. 之后才能发送 movement / property delta / RPC

Leave:
  1. 停止排队普通 delta
  2. 可靠发送 despawn/hide
  3. 清理连接侧对象状态
```

如果移动增量先于 spawn 到达，客户端会收到未知对象更新；如果 leave 后仍有普通增量排队，客户端会出现对象复活或幽灵状态。复制系统应按对象和连接维护 lifecycle state。

### 7.2 Late Join 和初始快照

新连接进入热点区域时，初始快照通常比移动更新更昂贵。建议：

- 首帧只发送玩家自身、控制目标、近距离关键对象和必要全局状态。
- 其余对象按距离和优先级分批 spawn。
- 对静态对象使用快照缓存或 chunk manifest，避免每个连接重新序列化。
- 对高密度人群使用最大 spawn 数量和低频 LOD，先保证可玩性。

### 7.3 最新值覆盖和可靠性

BigWorld release notes 中的 `SendLatestOnly` 和 `IsReliable` 思路很适合 AOI 复制：

- 移动、朝向、动画参数通常只需要最新值。
- spawn、despawn、装备变化、技能命中、死亡必须可靠或具备确认机制。
- 低优先级对象的多个待发送位置包应合并为一个最新状态。
- 每个连接应统计被覆盖、被丢弃、被延迟的更新数量，作为热点降级信号。

## 8. 分布式大世界 AOI

### 8.1 Zone 和 Map Instance

大多数 MMORPG 会先把世界拆为地图或区域。每个 Zone 由一个进程或一组进程负责。Zone 内再用 AOI。这样能把“全世界 N”拆成“每张地图 n”。

地图副本和 Megaserver 不是 AOI，但它们是控制 AOI 输入规模的关键手段。地图满了开新实例，比尝试让一个 AOI 支撑无限玩家更现实。

### 8.2 Cell Server、Ghost 和 Handoff

无缝大世界的典型模式：

- 世界被拆成多个 Cell。
- 每个 Cell 对内部实体有权威。
- Cell 边界附近的实体会在邻居 Cell 创建 ghost。
- 玩家 AOI 跨边界时，从本地真实实体和邻居 ghost 构建可见列表。
- 实体跨 Cell 时执行 handoff，权威从旧 Cell 转移到新 Cell。

BigWorld 文档公开描述了这种结构：CellApp 管理 Cell，边界附近创建 ghost，客户端实体周期性构建 AOI 更新包，CellAppMgr 可以通过改变 Cell 尺寸做负载均衡。

关键工程约束：

- ghost 区宽度必须覆盖最大可见半径和必要的预测距离。
- ghost 只保存远端查询和表现所需状态，不能拥有权威逻辑。
- handoff 必须保证消息有序，避免旧 Cell 和新 Cell 同时接受客户端命令。
- 跨边界技能、投射物、仇恨和寻路要有明确权威归属。

### 8.3 动态负载均衡

Cell 可以固定边界，也可以按负载调整：

- 低负载 Cell 扩大，减少进程间同步。
- 高负载 Cell 缩小，把部分实体 offload 到邻居。
- 热点战斗如果所有实体仍在同一小区域，继续切分会受到交互密度限制。

动态边界对实现要求很高。它需要 ghost、handoff、路由、状态迁移、跨 Cell 查询和监控体系先成熟。

### 8.4 过载保护

当所有人聚到同一个 AOI，空间过滤会失效。可选手段：

- 人数上限：地图上限、战场上限、区域排队。
- 连接预算：限制每连接每 tick 最大发送实体数。
- 低频 LOD：远处玩家降到 1 到 3 Hz。
- 聚合表现：远处人群变成团块、旗帜、计数或低精度状态。
- 技能/投射物降级：非关键特效不广播或只广播给近处。
- 时间膨胀：像 EVE Online 一样减慢模拟时间以保持队列可控和公平。

这些是玩法和技术共同决策，不是 AOI 数据结构能单独解决的问题。

### 8.5 云编排与 AOI 的边界

Agones、GameLift、Kubernetes、FleetIQ、FlexMatch 这类系统负责“把玩家放到哪个服务器进程”和“如何扩缩专用服务器”。它们不能替代服务器进程内部的 AOI：

- 编排能增加地图实例、房间实例或战场实例数量。
- 匹配能控制每场人数、地域和延迟。
- 自动伸缩能避免没有可用进程。
- 但当 500 人已经进入同一个地图实例并聚到同一坐标，单进程内仍需要 AOI、复制预算和热点降级。

因此架构上应把“会话放置/服务器分配”和“进程内 AOI”分成两个问题。

## 9. 当前 evpp3 AOI 实现评估

相关文件：

- `src/runtime/aoi/spatial_index.h`
- `src/runtime/aoi/spatial_index.cc`
- `src/runtime/aoi/aoi_manager.h`
- `src/runtime/aoi/aoi_manager.cc`
- `src/runtime/script/aoi_bind.cc`
- `src/client/client.h`
- `src/client/client_runtime.cpp`
- `resources/api/aoi/api.md`
- `src/tests/unit/aoi/test_aoi.cpp`
- `src/tests/integration/entity/test_entity_aoi.cpp`
- `src/tests/unit/client_api/test_client_api.cpp`
- `src/tests/performance/bench_aoi.cpp`

### 9.1 当前能力

`SpatialGrid`：

- 固定宽高二维网格。
- `vector<vector<CellEntry>>` 存储。
- `entity_cell_` 保存实体到格子和下标的映射。
- 插入、跨格更新、删除使用 `swap-remove`。
- `QueryRadius` 先算半径覆盖格子，再用平方距离精确过滤。
- `QueryAOI` 和 `QueryAOIAt` 返回 9 宫格候选。
- 越界位置会 clamp 到边界格。

`AOIManager`：

- 保存 `aoi_radii_`、`positions_`、`visible_`。
- 每个 observer 独立维护可见集合，语义是方向性可见。
- 移动时重算移动者和旧/新位置附近可能受影响的 observer。
- enter/leave 事件通过回调同步派发。
- `max_aoi_radius_` 用于找可能受移动影响的 observer。

Lua 绑定：

- `aoi.init(world_width, world_height, cell_size)`
- `aoi.register_entity(entity_id, x, y, aoi_radius)`
- `aoi.update_entity(entity_id, x, y)`
- `aoi.unregister_entity(entity_id)`
- `aoi.get_visible(entity_id)`
- `aoi.query_radius(x, y, radius)`
- `aoi.count()`
- `aoi.shutdown()`
- `aoi.set_event_callback(callback)`

C Client API：

- `game_aoi_create` / `game_aoi_destroy`
- `game_aoi_register_entity`
- `game_aoi_move_entity`
- `game_aoi_update_radius`
- `game_aoi_unregister_entity`
- `game_aoi_count`
- `game_aoi_query_radius`
- `game_aoi_get_visible`

这层 API 包装同一个 `AOIManager`，可以创建、注册、移动、注销、查询半径和读取可见集合；当前没有暴露 enter/leave 回调，也没有批量查询、空间/层过滤或连接复制调度接口。

### 9.2 优点

- 简洁，适合作为 Zone 内 AOI 基础。
- 查询使用精确半径，不容易出现 9 宫格漏查。
- 删除和跨格移动的数据结构高效。
- 输入校验覆盖 NaN、无限值和非法尺寸。
- `OnEntityMove` 会重算受影响 observer，不是只重算移动者自己。

### 9.3 已固化的契约澄清

1. 统一方向性可见语义：`A` 半径大能看到 `B`，不代表 `B` 能看到 `A`；`resources/api/aoi/api.md` 不再描述为双向可见。
2. 补全事件语义：`update_entity` 会重算移动者和附近受影响 observer，`unregister_entity` 会同时清理被删除实体自己的 visible 集合和其他 observer 对它的引用。
3. 区分错误语义：Lua 无效参数是参数错误，未初始化和回调重入返回 `nil, err`，C Client API 返回 `game_error_t`。
4. 明确结果顺序：`GetVisibleEntities` / `get_visible` 已排序，`QueryRadius` / `query_radius` / `QueryAOI` 不提供稳定顺序契约。
5. 明确 `register_entity` 对已有 id 是 upsert，普通移动应使用 `update_entity` / `game_aoi_move_entity`。
6. 明确坐标越界 clamp 到边界格，但精确距离仍使用原始坐标。
7. 明确 `QueryRadius` 不排除自身，调用方需要自行过滤。
8. 补齐 `aoi.init` 替换语义：参数错误仍是 Lua 参数错误；初始化阶段捕获到底层异常时返回 `nil, err`，且旧 AOI 实例不应被破坏。
9. 明确 `QueryAOI` 只是 9 格候选接口，任意半径查询应使用 `QueryRadius`。
10. 补充 C Client AOI 注释：方向性可见、upsert、clamp、count-only 查询、排序和 self-filter 语义已写入 `src/client/client.h`。

### 9.4 仍需代码或架构补强的地方

1. 缺少 `space_id`、`layer_id`、`phase_id`。现在所有实体默认在同一世界，不能表达副本、频道、任务相位。
2. 缺少逻辑过滤。阵营、队伍、隐身、对象类型、owner-only 状态无法参与 AOI。
3. 缺少 target-side aura。当前 target 被当作点对象，不能表达“大型对象、音源、远景建筑可以被更远感知”的规则。
4. 已增加反向 `watchers_` 索引。删除实体不再扫描全部 `visible_`，后续大规模优化应继续关注 watcher churn 和内存占用。
5. 缺少批处理。每次移动立即重算并同步派发事件，移动频率高时会浪费 CPU。
6. 缺少线程/阶段所有权模型。当前 AOI 不是线程安全组件，网络层、逻辑层和脚本层如果跨线程调用，必须先串行化。
7. 已增加 C++ callback 重入保护。回调内递归修改同一个 manager 会失败，不会破坏 `SpatialGrid`、`visible_` 或 `watchers_` 状态。
8. 缺少复制调度。AOI 只返回列表，没有按连接预算、优先级、LOD 发包。
9. 缺少热点保护。没有最大可见数、距离排序、区域限流、聚合或降级策略。
10. 缺少静态/动态分层。大量静态对象会和移动对象混在同一个索引层。
11. 缺少 Z/楼层/遮挡。当前是二维距离，无法表达多楼层、飞行或墙体视线。
12. 固定数组不适合无限大世界。超大地图会浪费内存或触发格子数安全上限。
13. 缺少 enter/leave 与后续移动 delta 的连接侧顺序模型。
14. 同一 observer 的多个 enter/leave 事件目标顺序未排序，不能作为确定性网络协议顺序使用。
15. C Client API 仅暴露查询型 AOI 能力，未暴露 enter/leave 事件、批处理或复制预算。

### 9.5 当前文档与资料限制

- 本轮链接检查日期：2026-06-02。第 15 节全部 19 个参考链接均可访问；其中 Unity 的 `docs.unity.com/multiplayer/netcode/netcode` 和旧 `docs-multiplayer.unity3d.com` 链接会重定向到新版文档入口。后续维护时应重新检查，因为 Unity、Photon、Mirror、AWS 和 Agones 文档会持续更新。
- BigWorld 资料来自 2012 年左右公开文档，但 Cell/Ghost/Handoff 仍是 MMO 分布式世界的经典参考。不能把 BigWorld 的具体进程名直接当作 evpp3 目标实现。
- Unity Netcode 的旧 `docs-multiplayer` 站点已转向新版 package 文档；Object Visibility 概念稳定，但具体 API 要以 `com.unity.netcode.gameobjects` 当前包文档和项目锁定版本为准。
- Photon Fusion Unreal 的 interest key 资料适合说明 key 订阅和空间哈希策略；不同 Photon 产品线的 API 不完全相同。
- EVE Time Dilation 是过载治理案例，不是 AOI 算法；它用于说明热点无法继续空间裁剪时的降级思路。
- GameLift/Agones 是托管和编排层，不是 AOI 中间件。

### 9.6 已有测试覆盖与剩余缺口

本轮对照 `src/tests/unit/aoi/test_aoi.cpp`、`src/tests/integration/entity/test_entity_aoi.cpp`、`src/tests/unit/client_api/test_client_api.cpp` 和 `src/tests/performance/bench_aoi.cpp`，当前测试已经覆盖：

- `SpatialGrid` 构造参数校验、插入、重复插入更新、删除、跨格移动、`QueryRadius`、9 宫格 `QueryAOI`、越界坐标 clamp、负半径安全返回。
- `AOIManager` 注册/注销计数、非法半径/位置、移动进入/离开、方向性可见、相同半径下的对称可见、未注册实体移动忽略、无回调时安全。
- Lua 绑定导出、`aoi.count()`、跨 `ScriptVM` 状态隔离、回调异常恢复、回调内 `shutdown()` mutation guard、基本 symmetric callbacks、非法 init 参数。
- C Client API 的 `game_aoi_create/register_entity/move_entity/update_radius/query_radius/get_visible/unregister_entity/count` 基本路径。
- 性能基准覆盖均匀查询、拥挤格跨格更新和 `AOIManager` crowd move。

仍缺少或需要加强：

- `register_entity` 对已有 id 的 upsert 行为和事件序列测试。
- `aoi.init` 重新初始化成功后应清理旧实体和旧 callback；初始化失败时应保留旧 AOI 状态；`aoi.shutdown` 后 `count/get_visible/query_radius` 的空状态语义测试。
- Lua 回调重入目前只覆盖 `shutdown()`；还需要覆盖 `init`、`register_entity`、`update_entity`、`unregister_entity` 和 `set_event_callback`。
- Lua 非法参数错误与 `nil, err` 返回路径的区分测试。
- `QueryRadius` 包含自身的 API 语义测试。
- `QueryRadius` / `QueryAOI` 未排序结果不作为协议顺序的契约测试。
- `QueryAOI` 只覆盖同格和邻格，不可用于任意半径查询的边界测试。
- `aoi_radius = 0` 的同坐标可见测试。
- 越界坐标原始位置参与精确距离过滤的测试。
- 多目标 enter/leave 的稳定排序或明确非稳定契约测试。
- `max_aoi_radius_` 在半径缩小、实体删除、重新注册后的边界测试。
- C++ callback 递归修改 AOI 的禁止策略或 guard 测试。
- C Client API 对无效参数、`out_ids == nullptr` 计数查询、容量不足返回 `GAME_ERR_BUFFER_TOO_SMALL`、方向性可见和 upsert 语义的边界测试。
- 客户端复制层的 spawn/delta/despawn 顺序测试，目前 AOI 单元测试无法覆盖。

## 10. evpp3 推荐路线

### P0：澄清语义和测试

目标：让当前 AOI 成为可信的单区服基础。

- 明确方向性可见：`visible_[observer]` 表示 observer 能看到 target。
- 已完成文档同步：`resources/api/aoi/api.md` 和 `src/client/client.h` 已写明方向性可见、事件语义、错误语义、upsert、clamp、self-filter 和结果顺序。
- 保留现有方向性可见、注册后可见、注销 leave、越界 clamp 测试作为回归用例，不再把这些已覆盖项当作缺口。
- 补齐测试：
  - 已有 id 重新 `register_entity` 的 upsert 行为、半径变化和事件序列。
  - `aoi.init` 成功 reset、失败时保留旧状态、`aoi.shutdown` 后空状态、旧 callback 清理。
  - `aoi_radius = 0` 同坐标可见。
  - `QueryRadius` 包含自身、`GetVisibleEntities` 排除自身。
  - `QueryRadius` / `QueryAOI` 未排序结果的非契约说明。
  - `QueryAOI` 只返回同格和邻格候选，不承担任意半径查询。
  - 越界坐标 clamp 到格子但保留原始坐标参与距离过滤。
  - `max_aoi_radius_` 在半径缩小、删除和重新注册后的边界行为。
  - Lua 回调内所有 mutation API 都应返回错误且不破坏状态。
  - C++ callback 重入修改同一个 manager 的禁止策略或 guard。
  - C Client API 的非法参数、空输出指针计数查询、容量不足、方向性可见和 upsert。
- 给 `QueryAOI` 标注使用限制，避免被误用为任意半径查询。
- 给 `register_entity` 明确“新增还是 upsert”。当前保留 upsert，并已增加单独 `aoi.update_radius(entity_id, radius)` / `game_aoi_update_radius`，避免注册接口承担移动和半径更新双重语义。
- 保持 API 文档和 C Client API 说明与实现同步：坐标越界 clamp、clamp 不改写原始坐标、`QueryRadius` 包含自身、`GetVisibleEntities` 排除自身、非线程安全、回调同步派发、回调期间禁止修改、`aoi.count()` 和 `aoi.shutdown()` 都应作为契约保留。
- 增加事件顺序测试：enter 必须先于该 target 的移动 delta，leave 后不能再发送普通 delta。
- 若网络层需要可复现事件顺序，应在 AOI 事件派发前排序目标 id；否则在 API 文档中声明同一 observer 的多目标事件顺序不稳定。

### P1：空间和规则过滤

目标：支持真实 MMO 的地图、副本和玩法可见性。

新增结构建议：

```cpp
struct AOIKey {
    uint32_t space_id;
    uint16_t layer_id;
    uint16_t phase_id;
};

struct AOIProfile {
    float enter_radius;
    float leave_radius;
    float aura_radius;
    uint32_t category_mask;
    uint32_t visible_category_mask;
    uint32_t max_visible;
    uint8_t update_lod;
};

using VisibilityPredicate =
    std::function<bool(EntityId observer, EntityId target)>;
```

实现建议：

- 每个 `AOIKey` 一个 `SpatialGrid` 或 `SpatialHashGrid`。
- `AOIManager::RegisterEntity` 带 `space/layer/phase/profile`。
- `QueryRadius` 只在同一 `AOIKey` 内查。
- 距离判断可先按 `observer.enter_radius + target.aura_radius` 进入，按 `observer.leave_radius + target.aura_radius` 离开；不需要 target aura 的对象把 `aura_radius` 设为 0。
- 再执行 category 和 gameplay predicate。
- 保留并扩展当前 `watchers_` 反向表。
- 支持空间切换：旧 key leave，新 key enter，不能跨 key 直接移动。
- 支持静态层和动态层分开索引。

### P2：批处理和复制调度

目标：把 AOI 从“查询列表”升级为“连接复制输入”。

建议：

- 移动只更新位置并标 dirty。
- 每 50 到 200 ms 批处理 dirty 实体。
- enter/leave 可靠派发，移动状态走最新值覆盖。
- 为每个连接维护优先队列和预算。
- 支持 `AOIUpdateScheme`：
  - critical：每 tick。
  - near：10 Hz。
  - mid：5 Hz。
  - far：1 到 2 Hz。
  - dormant：变化时。
- 静态对象和动态对象分层索引。
- 为连接建立 lifecycle state，防止 spawn/delta/despawn 乱序。
- 对移动类属性使用 latest-only 合并。

### P3：热点保护

目标：主城和团战不把服务器打爆。

建议：

- `max_visible`：每个 observer 最多保留 K 个同类目标。
- 距离排序加权：距离、战斗关系、队伍、目标锁定、威胁度。
- 区域密度超过阈值时启用二级网格。
- 远距实体降为低频或聚合状态。
- 监控候选数、实际可见数、enter/leave churn、发送预算耗尽次数。
- 支持可配置热点策略：拒绝进入、排队、迁移实例、降低 LOD、限制同屏人数、时间膨胀。

### P4：分布式 Cell 和 Ghost

目标：无缝大世界。

前置条件：P1 到 P3 稳定后再做。

建议：

- 定义 Cell 权威边界和 ghost 边界。
- ghost 宽度大于最大 AOI 半径和移动预测距离。
- Cell 间同步只发送 ghost 必需属性。
- handoff 使用事务式流程：冻结旧权威、迁移状态、新权威确认、路由切换、旧权威释放。
- Base/Proxy 层隔离客户端，不让客户端感知 Cell 切换。
- 明确跨 Cell 权威规则：技能、投射物、召唤物、仇恨、掉落和寻路不能同时由两个 Cell 决策。

## 11. 指标和压测

AOI 必须可观测。建议至少记录：

- `aoi.entities`
- `aoi.cells.active`
- `aoi.cell.max_entities`
- `aoi.query.count`
- `aoi.query.candidates`
- `aoi.query.result_count`
- `aoi.visible.total`
- `aoi.enter.count`
- `aoi.leave.count`
- `aoi.dirty.count`
- `aoi.recompute.duration_us`
- `aoi.dispatch.duration_us`
- `aoi.connection.bytes_budget_used`
- `aoi.connection.entity_budget_dropped`
- `aoi.latest_only.coalesced`
- `aoi.lifecycle.out_of_order_prevented`
- `aoi.hotspot.active_regions`
- `aoi.callback.errors`

压测场景：

1. 均匀分布 10k、50k、100k 实体。
2. 主城热点：100、500、1000 实体挤在一个或几个格子。
3. 边界移动：大量实体穿越格子边界。
4. 瞬移：实体跨越多个格子。
5. 大小半径混合：普通玩家和超大视野实体混合。
6. 删除风暴：大量实体同时 despawn。
7. 队伍/相位/隐身逻辑过滤。
8. late join：新连接进入热点区的初始快照预算。
9. re-register/upsert：已有 id 重新注册不同半径和位置。
10. 越界输入：负坐标、超过世界宽高、边界半径查询。
11. 回调重入：回调内尝试修改 AOI 应返回错误且不破坏状态。
12. 生命周期顺序：spawn、delta、despawn 的连接侧排序。

## 12. 常见陷阱

- 把客户端渲染裁剪当成 AOI。客户端不渲染不代表服务器可以把状态发过去。
- 只做距离过滤，忘记相位、队伍、隐身和权限。
- 只更新移动者的 visible，忘记移动者也会进入/离开别人的 AOI。
- enter/leave 与移动增量乱序，客户端收到移动包时对象还未 spawn。
- 事件回调中再次修改 AOI，导致迭代器失效或递归事件。
- 热点区不设上限，最终所有候选都互相可见。
- 用一个全局最大半径拖慢所有查询。
- 删除实体时只从网格删除，忘记清理 visible/watchers。
- 固定 9 宫格查询用于任意半径，导致漏查。
- 试图用 Kubernetes 或云伸缩替代进程内 AOI。编排只能增加房间/地图进程，不能降低单热点内的 N² 交互。
- 把 re-register 当普通移动使用，导致半径和旧位置先触发一轮事件。
- 忘记区分 `query_radius` 的通用查询和 `get_visible` 的观察者可见集合。
- 把 `QueryRadius` 或 `QueryAOI` 的返回顺序当作协议顺序；当前只有 `GetVisibleEntities` 明确排序。
- 误以为越界坐标被 clamp 后原始位置也被改写；当前只是落入边界格，精确距离仍按原始坐标算。
- 忘记连接侧生命周期状态，导致 delta 早于 spawn 或晚于 despawn。

## 13. 推荐架构摘要

evpp3 可以按下面的最终形态演进：

```text
AOISystem
  AOISpaceRegistry
    AOISpace(space_id, layer_id, phase_id)
      SpatialIndex
        DenseGrid | SparseHashGrid | PagedGrid
      EntityTable
      VisibleTable
      WatcherTable
      DirtyQueue
      RuleFilters
      Metrics

ReplicationSystem
  ConnectionState
    interest_profile
    reliable_lifecycle_queue
    priority_state_queue
    byte_budget
    entity_budget

WorldPartition
  ZoneServer
  CellServer
  GhostReplicator
  HandoffCoordinator
```

短期保留当前 `SpatialGrid + AOIManager` 是正确选择。它应作为“单 Zone 内精确半径 AOI”的基础，而不是直接承担全世界扩展。下一步最有价值的是语义修正、空间/层隔离、反向 watchers 和批处理调度。

## 14. 术语与契约速查

| 项 | 当前 evpp3 契约 | 生产 MMO 扩展方向 |
| --- | --- | --- |
| 空间范围 | 单个固定二维世界，`SpatialGrid` 预分配全部格子 | `space_id/layer_id/phase_id` 多空间注册表，稀疏或分页网格 |
| 可见方向 | 方向性；`visible_[observer]` 表示 observer 能看到 target | 可选对称规则、target-side aura、玩法谓词 |
| 查询 API | `QueryRadius` 是精确半径查询；`QueryAOI` 是 9 格候选 | 多层索引、类型分层、批量查询 |
| 结果顺序 | `GetVisibleEntities` 排序；半径/9 格查询不承诺顺序 | 网络协议层显式排序和稳定事件批次 |
| 初始化 | `aoi.init` 成功后替换旧实例；失败时保留旧实例 | 配置热切换、灰度空间迁移、失败回滚 |
| 回调 | Lua 回调同步派发且有 mutation guard；C++ 回调无 guard | 统一事件队列、重入保护、异步批处理 |
| 删除 | 清理自身 visible，并通过 `watchers_` 找到看见自己的 observers | 更大规模下继续优化 watcher churn、分层索引和批量删除 |
| 热点 | 无 `max_visible`、LOD、预算或降级 | 连接预算、距离排序、低频 LOD、实例迁移 |
| 线程 | 无内部锁，要求单线程或阶段所有权 | actor ownership、任务队列、跨线程只投递命令 |
| 复制 | AOI 只给候选和 enter/leave | spawn/delta/despawn 生命周期队列和复制预算 |

## 15. 参考资料

- IBM Research / ACM Computing Surveys: [Interest management for distributed virtual environments: A survey](https://research.ibm.com/publications/interest-management-for-distributed-virtual-environments-a-survey)
- Springer: [Area of Interest Management in Massively Multiplayer Online Games](https://link.springer.com/rwe/10.1007/978-3-319-08234-9_239-1)
- Epic Games: [Replication Graph in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/replication-graph-in-unreal-engine)
- Unity Multiplayer: [Unity's netcode packages](https://docs.unity.com/multiplayer/netcode/netcode)
- Unity Netcode for GameObjects package: [latest package docs](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@latest/)
- Unity Multiplayer archive: [Netcode for GameObjects Object visibility](https://docs-multiplayer.unity3d.com/netcode/2.3.2/basics/object-visibility/)
- Photon Fusion Unreal: [Interest Management](https://doc.photonengine.com/fusion-unreal/current/manual/replication/interest-management)
- Photon Server: [Interest Groups](https://doc.photonengine.com/server/current/applications/loadbalancing/interestgroups)
- Photon Blog: [Photon Fusion Area of Interest sample](https://blog.photonengine.com/new-photon-fusion-area-of-interest-sample/)
- Mirror Networking: [Interest Management](https://mirror-networking.gitbook.io/docs/manual/interest-management)
- BigWorld Server Overview: [Design Introduction](https://howarduong.github.io/github.io/doc/html/server_overview/ch04.html)
- BigWorld Server Release Notes: [AOI callbacks and update scheme notes](https://howarduong.github.io/github.io/doc/release_notes_server.html)
- EVE Online: [Introducing Time Dilation](https://www.eveonline.com/news/view/introducing-time-dilation-tidi)
- Guild Wars 2: [Introducing the Megaserver System](https://www.guildwars2.com/en/news/introducing-the-megaserver-system/)
- Guild Wars 2: [Continued Improvements to the Megaserver System](https://www.guildwars2.com/en/news/continued-improvements-to-the-megaserver-system/)
- Agones: [Overview](https://agones.dev/site/docs/overview/)
- Amazon GameLift Servers: [Documentation overview](https://aws.amazon.com/documentation-overview/gamelift/)
- Gaffer On Games: [State Synchronization](https://gafferongames.com/post/state_synchronization/)
- Gaffer On Games: [Snapshot Compression](https://gafferongames.com/post/snapshot_compression/)
