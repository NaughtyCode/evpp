#include "runtime/space/connection_router.h"

#include "runtime/core/log/log.h"
#include "runtime/space/space_manager.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace space {

ConnectionRouter& ConnectionRouter::Instance() {
	static ConnectionRouter instance;
	return instance;
}

entity::EntityId ConnectionRouter::RouteNewConnection(SpaceId space_id,
                                                       evpp::TCPConnPtr conn) {
	auto& manager = SpaceManager::Instance();
	auto* space = manager.GetSpace(space_id);
	if (!space) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ConnectionRouter: space [{}] not found", space_id);
		return entity::kInvalidEntityId;
	}

	auto* entity = space->CreateEntity();
	if (!entity) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ConnectionRouter: failed to create entity in space [{}]",
						 space_id);
		return entity::kInvalidEntityId;
	}

	entity::EntityId eid = entity->GetId();
	space->OnPlayerJoin(eid, conn);

	{
		std::lock_guard<std::mutex> lock(mutex_);
		conn_to_space_[conn.get()] = space_id;
		conn_to_entity_[conn.get()] = eid;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ConnectionRouter: connection routed to space [{}], entity [{}]",
					space_id, eid);
	return eid;
}

void ConnectionRouter::RouteMessage(evpp::TCPConnPtr conn, const std::string& data) {
	SpaceId space_id;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = conn_to_space_.find(conn.get());
		if (it == conn_to_space_.end()) return;
		space_id = it->second;
	}

	auto* space = SpaceManager::Instance().GetSpace(space_id);
	if (!space) return;

	auto* L = space->GetLuaState();
	if (!L) return;

	// Deliver to Lua via on_data callback
	lua_getglobal(L, "space");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	lua_getfield(L, -1, "_on_connection_data");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return;
	}

	// Push connection pointer as lightuserdata and data string
	lua_pushlightuserdata(L, conn.get());
	lua_pushlstring(L, data.data(), data.size());

	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
						 "ConnectionRouter: message delivery error: {}",
						 lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	lua_pop(L, 1);  // space table
}

void ConnectionRouter::RouteDisconnection(evpp::TCPConnPtr conn) {
	SpaceId space_id;
	entity::EntityId eid;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto sit = conn_to_space_.find(conn.get());
		if (sit == conn_to_space_.end()) return;
		space_id = sit->second;

		auto eit = conn_to_entity_.find(conn.get());
		if (eit != conn_to_entity_.end()) {
			eid = eit->second;
		}

		conn_to_space_.erase(sit);
		conn_to_entity_.erase(conn.get());
	}

	auto* space = SpaceManager::Instance().GetSpace(space_id);
	if (space && eid != entity::kInvalidEntityId) {
		space->OnPlayerLeave(eid);
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ConnectionRouter: connection disconnected from space [{}]", space_id);
}

SpaceId ConnectionRouter::FindSpaceByConnection(const evpp::TCPConn* raw_conn) const {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = conn_to_space_.find(raw_conn);
	if (it == conn_to_space_.end()) return kInvalidSpaceId;
	return it->second;
}

}  // namespace space
}  // namespace engine
