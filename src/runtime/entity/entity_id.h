#pragma once

#include <atomic>
#include <cstdint>

namespace engine {
namespace entity {

using EntityId = uint64_t;
inline constexpr EntityId kInvalidEntityId = 0;

class EntityIdAllocator {
public:
	virtual ~EntityIdAllocator() = default;
	virtual EntityId Allocate() = 0;
	virtual void Release(EntityId /*id*/) = 0;
};

class SequentialIdAllocator : public EntityIdAllocator {
public:
	EntityId Allocate() override {
		return next_id_.fetch_add(1, std::memory_order_relaxed);
	}
	void Release(EntityId /*id*/) override {
		// Sequential allocator does not reuse IDs.
	}

private:
	std::atomic<EntityId> next_id_{1};
};

}  // namespace entity
}  // namespace engine
