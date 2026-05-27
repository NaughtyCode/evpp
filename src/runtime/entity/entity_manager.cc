#include "entity_manager.h"

namespace engine {
namespace entity {

EntityManager& EntityManager::Instance() {
	static EntityManager mgr;
	return mgr;
}

Entity* EntityManager::CreateEntity(EntityId id) {
	if (id == kInvalidEntityId) {
		id = id_allocator_.Allocate();
	}
	if (entities_.find(id) != entities_.end()) {
		return nullptr;  // ID collision
	}
	auto entity = std::make_unique<Entity>(id);
	Entity* raw = entity.get();
	entities_[id] = std::move(entity);
	return raw;
}

Entity* EntityManager::GetEntity(EntityId id) {
	auto it = entities_.find(id);
	return it != entities_.end() ? it->second.get() : nullptr;
}

void EntityManager::DestroyEntity(EntityId id) {
	auto it = entities_.find(id);
	if (it == entities_.end()) return;
	it->second->Destroy();
	entities_.erase(it);
}

void EntityManager::DestroyAll() {
	for (auto& pair : entities_) {
		pair.second->Destroy();
	}
	entities_.clear();
	conn_to_entity_.clear();
}

Entity* EntityManager::FindByConnection(const evpp::TCPConnPtr& conn) {
	if (!conn) return nullptr;
	auto it = conn_to_entity_.find(conn.get());
	if (it == conn_to_entity_.end()) return nullptr;
	return GetEntity(it->second);
}

void EntityManager::RegisterConnectionBinding(const evpp::TCPConn* raw_conn, EntityId id) {
	conn_to_entity_[raw_conn] = id;
}

void EntityManager::UnregisterConnectionBinding(const evpp::TCPConn* raw_conn) {
	conn_to_entity_.erase(raw_conn);
}

void EntityManager::ForEachActive(std::function<void(Entity&)> callback) {
	for (auto& pair : entities_) {
		if (pair.second->GetState() == EntityState::Active) {
			callback(*pair.second);
		}
	}
}

size_t EntityManager::Count() const {
	return entities_.size();
}

size_t EntityManager::ActiveCount() const {
	size_t count = 0;
	for (const auto& pair : entities_) {
		if (pair.second->GetState() == EntityState::Active) {
			++count;
		}
	}
	return count;
}

}  // namespace entity
}  // namespace engine
