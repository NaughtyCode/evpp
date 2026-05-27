#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <runtime/evpp/tcp_callbacks.h>

#include "entity.h"
#include "entity_id.h"

namespace engine {
namespace entity {

class EntityManager {
public:
	static EntityManager& Instance();

	EntityManager(const EntityManager&) = delete;
	EntityManager& operator=(const EntityManager&) = delete;

	// Create an entity. id=0 means auto-allocate.
	Entity* CreateEntity(EntityId id = 0);
	Entity* GetEntity(EntityId id);
	void DestroyEntity(EntityId id);
	void DestroyAll();

	// Connection lookup — uses raw pointer since connection lifetime is
	// managed by the event loop, not by entities.
	Entity* FindByConnection(const evpp::TCPConnPtr& conn);

	// Register/unregister a connection→entity binding for O(1) lookup.
	void RegisterConnectionBinding(const evpp::TCPConn* raw_conn, EntityId id);
	void UnregisterConnectionBinding(const evpp::TCPConn* raw_conn);

	// Iteration
	void ForEachActive(std::function<void(Entity&)> callback);
	size_t Count() const;
	size_t ActiveCount() const;

private:
	EntityManager() = default;
	~EntityManager() = default;

	std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
	std::unordered_map<const evpp::TCPConn*, EntityId> conn_to_entity_;
	SequentialIdAllocator id_allocator_;
};

}  // namespace entity
}  // namespace engine
