#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <runtime/evpp/tcp_callbacks.h>

#include "entity.h"
#include "entity_id.h"
#include "runtime/core/engine_api.h"

namespace engine {

class TimerManager;

namespace entity {

class CLOUD_ENGINE_API EntityManager {
public:
	using EntityDestroyHook = std::function<void(Entity&)>;

	static EntityManager& Instance();

	void SetTimerManager(TimerManager* tm);
	TimerManager* GetTimerManager() const { return timer_mgr_; }
	void SetDestroyHook(EntityDestroyHook hook) { on_destroy_ = std::move(hook); }
	void NotifyEntityDestroying(Entity& entity);

	EntityManager(const EntityManager&) = delete;
	EntityManager& operator=(const EntityManager&) = delete;

	// Create an entity. id=0 means auto-allocate.
	Entity* CreateEntity(EntityId id = 0);
	Entity* GetEntity(EntityId id);
	bool OwnsEntity(const Entity& entity) const;
	void DestroyEntity(EntityId id);
	void DestroyAll();

	// Connection lookup — uses raw pointer since connection lifetime is
	// managed by the event loop, not by entities.
	Entity* FindByConnection(const evpp::TCPConnPtr& conn);
	Entity* FindByConnection(const evpp::TCPConn* raw_conn);

	// Register/unregister a connection→entity binding for O(1) lookup.
	bool RegisterConnectionBinding(const evpp::TCPConn* raw_conn, EntityId id);
	void UnregisterConnectionBinding(const evpp::TCPConn* raw_conn);

	// Physics body lookup
	Entity* FindByPhysicsBodyId(uint32_t body_id);
	void RegisterPhysicsBodyBinding(uint32_t body_id, EntityId id);
	void UnregisterPhysicsBodyBinding(uint32_t body_id);

	// Iteration
	void ForEachActive(std::function<void(Entity&)> callback);
	size_t Count() const;
	size_t ActiveCount() const;

private:
	EntityManager() = default;
	~EntityManager();

	std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
	std::unordered_map<const evpp::TCPConn*, EntityId> conn_to_entity_;
	std::unordered_map<uint32_t, EntityId> body_to_entity_;
	SequentialIdAllocator id_allocator_;
	TimerManager* timer_mgr_ = nullptr;
	EntityDestroyHook on_destroy_;
};

}  // namespace entity
}  // namespace engine
