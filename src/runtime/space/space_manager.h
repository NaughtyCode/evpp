#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"
#include "runtime/space/space.h"

namespace engine {
namespace space {

//=============================================================================
// SpaceManager — manages all active Spaces
//=============================================================================

class ENGINE_API SpaceManager {
public:
	static SpaceManager& Instance();

	SpaceManager(const SpaceManager&) = delete;
	SpaceManager& operator=(const SpaceManager&) = delete;

	// Create a space with the given config. Returns nullptr on failure.
	Space* CreateSpace(const SpaceConfig& config);

	// Create with explicit ID (for reconnect/transfer).
	Space* CreateSpaceWithId(SpaceId id, const SpaceConfig& config);

	Space* GetSpace(SpaceId id);
	void DestroySpace(SpaceId id);
	size_t SpaceCount() const { return spaces_.size(); }

	// Iteration
	void ForEachSpace(std::function<void(Space&)> callback);

	// Default space for backward-compatible single-VM mode.
	Space* GetDefaultSpace();
	Space* CreateDefaultSpace(const SpaceConfig& config);

private:
	SpaceManager() = default;
	~SpaceManager() = default;

	std::unordered_map<SpaceId, std::unique_ptr<Space>> spaces_;
	std::atomic<SpaceId> next_space_id_{1};
	SpaceId default_space_id_ = kInvalidSpaceId;
};

}  // namespace space
}  // namespace engine
