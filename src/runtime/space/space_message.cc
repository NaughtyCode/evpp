#include "runtime/space/space_message.h"

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/space/space_manager.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace space {

SpaceMessageRouter& SpaceMessageRouter::Instance() {
	static SpaceMessageRouter instance;
	return instance;
}

void SpaceMessageRouter::SendMessage(SpaceMessage msg) {
	ENGINE_PROFILE_SPACE_MSG_SEND();
	pending_.enqueue(std::move(msg));
}

void SpaceMessageRouter::ProcessPending() {
	ENGINE_PROFILE_SPACE_MSG_PROCESS();
	SpaceMessage msg;
	size_t processed = 0;
	auto& manager = SpaceManager::Instance();

	while (processed < 256 && pending_.try_dequeue(msg)) {
		auto* target = manager.GetSpace(msg.target_space);
		if (!target) {
			auto* logger = GetLogger();
			ENGINE_LOG_WARN(logger,
							"SpaceMessageRouter: target space [{}] not found, "
							"dropping message from [{}]",
							msg.target_space, msg.source_space);
			++processed;
			continue;
		}

		auto* L = target->GetLuaState();
		if (!L) {
			++processed;
			continue;
		}

		// Deliver via Lua: push on_message fields and call entity handler
		lua_getglobal(L, "space");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			++processed;
			continue;
		}

		lua_getfield(L, -1, "_deliver_message");
		if (!lua_isfunction(L, -1)) {
			lua_pop(L, 2);
			++processed;
			continue;
		}

		// stack: space_table, _deliver_message
		lua_pushinteger(L, static_cast<lua_Integer>(msg.source_space));
		lua_pushinteger(L, static_cast<lua_Integer>(msg.source_entity));
		lua_pushinteger(L, static_cast<lua_Integer>(msg.target_entity));
		lua_pushlstring(L, msg.payload.data(), msg.payload.size());

		int msgh = PushLuaErrorHandlerForCall(L, 4);
		if (lua_pcall(L, 4, 0, msgh) != LUA_OK) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "SpaceMessageRouter: delivery error: {}",
							 lua_tostring(L, -1));
			lua_pop(L, 1);
		}

		lua_pop(L, 1);  // space table
		++processed;
	}
}

size_t SpaceMessageRouter::PendingCount() const {
	return pending_.size_approx();
}

}  // namespace space
}  // namespace engine
