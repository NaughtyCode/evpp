#include "runtime/space/space_manager.h"

#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace space {

SpaceManager& SpaceManager::Instance() {
	static SpaceManager instance;
	return instance;
}

Space* SpaceManager::CreateSpace(const SpaceConfig& config) {
	ENGINE_PROFILE_SPACE_CREATE();
	for (;;) {
		SpaceId id = next_space_id_.fetch_add(1, std::memory_order_relaxed);
		if (id == kInvalidSpaceId || spaces_.find(id) != spaces_.end()) {
			continue;
		}
		return CreateSpaceWithId(id, config);
	}
}

Space* SpaceManager::CreateSpaceWithId(SpaceId id, const SpaceConfig& config) {
	ENGINE_PROFILE_SPACE_CREATE();
	if (id == kInvalidSpaceId) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "SpaceManager: invalid space id [{}]", id);
		}
		return nullptr;
	}

	if (spaces_.find(id) != spaces_.end()) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "SpaceManager: space [{}] already exists", id);
		}
		return nullptr;
	}

	auto space = std::make_unique<Space>(id, config);
	auto* raw = space.get();
	spaces_[id] = std::move(space);

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "SpaceManager: created space [{}] name=[{}], total=[{}]",
						id, config.name, spaces_.size());
	}

	SpaceId expected = next_space_id_.load(std::memory_order_relaxed);
	while (expected <= id &&
		   !next_space_id_.compare_exchange_weak(expected,
												 id + 1,
												 std::memory_order_relaxed,
												 std::memory_order_relaxed)) {
	}
	return raw;
}

Space* SpaceManager::GetSpace(SpaceId id) {
	ENGINE_PROFILE_SPACE_GET();
	auto it = spaces_.find(id);
	if (it == spaces_.end()) return nullptr;
	return it->second.get();
}

void SpaceManager::DestroySpace(SpaceId id) {
	ENGINE_PROFILE_SPACE_DESTROY();
	auto* logger = GetLogger();
	auto it = spaces_.find(id);
	if (it == spaces_.end()) {
		if (logger) {
			ENGINE_LOG_DEBUG(logger, "SpaceManager: destroy ignored, space [{}] not found", id);
		}
		return;
	}

	if (id == default_space_id_) {
		if (logger) {
			ENGINE_LOG_INFO(logger, "SpaceManager: destroying default space [{}]", id);
		}
		default_space_id_ = kInvalidSpaceId;
	}
	spaces_.erase(it);
	if (logger) {
		ENGINE_LOG_INFO(logger, "SpaceManager: destroyed space [{}], remaining=[{}]",
						id, spaces_.size());
	}
}

size_t SpaceManager::SpaceCount() const {
	return spaces_.size();
}

void SpaceManager::ForEachSpace(std::function<void(Space&)> callback) {
	std::vector<SpaceId> ids;
	ids.reserve(spaces_.size());
	for (const auto& [id, space] : spaces_) {
		ids.push_back(id);
	}

	for (auto id : ids) {
		auto it = spaces_.find(id);
		if (it != spaces_.end()) {
			callback(*it->second);
		}
	}
}

Space* SpaceManager::GetDefaultSpace() {
	if (default_space_id_ == kInvalidSpaceId) return nullptr;
	return GetSpace(default_space_id_);
}

Space* SpaceManager::CreateDefaultSpace(const SpaceConfig& config) {
	ENGINE_PROFILE_SPACE_CREATE();
	if (default_space_id_ != kInvalidSpaceId) {
		return GetSpace(default_space_id_);
	}
	auto* space = CreateSpace(config);
	if (space) {
		default_space_id_ = space->GetId();
	}
	return space;
}

}  // namespace space
}  // namespace engine
