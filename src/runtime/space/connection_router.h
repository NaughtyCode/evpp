#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <runtime/evpp/tcp_callbacks.h>

#include "runtime/core/engine_api.h"
#include "runtime/entity/entity_id.h"
#include "runtime/space/space.h"

namespace engine {
namespace space {

//=============================================================================
// ConnectionRouter — routes incoming TCP connections to Spaces
//=============================================================================

class ENGINE_API ConnectionRouter {
public:
	static ConnectionRouter& Instance();

	ConnectionRouter(const ConnectionRouter&) = delete;
	ConnectionRouter& operator=(const ConnectionRouter&) = delete;

	// Assign a new connection to a space. Creates a player entity in the
	// target space and binds the connection. Returns the created entity ID,
	// or kInvalidEntityId on failure.
	entity::EntityId RouteNewConnection(SpaceId space_id, evpp::TCPConnPtr conn);

	// Route incoming data from a connection to its space.
	void RouteMessage(evpp::TCPConnPtr conn, const std::string& data);

	// Handle connection close — suspend the entity (not destroy).
	void RouteDisconnection(evpp::TCPConnPtr conn);

	// Look up which space a connection belongs to.
	SpaceId FindSpaceByConnection(const evpp::TCPConn* raw_conn) const;

private:
	ConnectionRouter() = default;
	~ConnectionRouter() = default;

	mutable std::mutex mutex_;
	std::unordered_map<const evpp::TCPConn*, SpaceId> conn_to_space_;
	std::unordered_map<const evpp::TCPConn*, entity::EntityId> conn_to_entity_;
};

}  // namespace space
}  // namespace engine
