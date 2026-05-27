#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <concurrentqueue.h>

#include "runtime/core/engine_api.h"
#include "runtime/entity/entity_id.h"
#include "runtime/space/space.h"

namespace engine {
namespace space {

//=============================================================================
// SpaceMessage — cross-space async message
//=============================================================================

struct SpaceMessage {
	SpaceId source_space = kInvalidSpaceId;
	SpaceId target_space = kInvalidSpaceId;
	entity::EntityId source_entity = entity::kInvalidEntityId;
	entity::EntityId target_entity = entity::kInvalidEntityId;
	std::string payload;
};

//=============================================================================
// SpaceMessageRouter — delivers cross-space messages via SPSC queue
//=============================================================================

class ENGINE_API SpaceMessageRouter {
public:
	static SpaceMessageRouter& Instance();

	SpaceMessageRouter(const SpaceMessageRouter&) = delete;
	SpaceMessageRouter& operator=(const SpaceMessageRouter&) = delete;

	// Enqueue a message for delivery. Thread-safe.
	void SendMessage(SpaceMessage msg);

	// Deliver all pending messages. Called each frame from the main thread.
	void ProcessPending();

	// Approximate count of pending messages.
	size_t PendingCount() const;

private:
	SpaceMessageRouter() = default;
	~SpaceMessageRouter() = default;

	moodycamel::ConcurrentQueue<SpaceMessage> pending_;
};

}  // namespace space
}  // namespace engine
