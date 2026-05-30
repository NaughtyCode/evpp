#include "entity_manager.h"

#include <vector>

#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace entity {

EntityManager& EntityManager::Instance() {
	static EntityManager mgr;
	return mgr;
}

EntityManager::~EntityManager() {
	DestroyAll();
}

void EntityManager::SetTimerManager(TimerManager* tm) {
	timer_mgr_ = tm;
	for (auto& pair : entities_) {
		pair.second->SetTimerManager(tm);
	}
}

void EntityManager::NotifyEntityDestroying(Entity& entity) {
	if (on_destroy_) {
		on_destroy_(entity);
	}
}

Entity* EntityManager::CreateEntity(EntityId id) {
	ENGINE_PROFILE_ENTITY_CREATE();
	if (id == kInvalidEntityId) {
		size_t attempts = 0;
		const size_t max_attempts = entities_.size() + 1;
		do {
			id = id_allocator_.Allocate();
			if (id != kInvalidEntityId && entities_.find(id) == entities_.end()) {
				break;
			}
			++attempts;
		} while (attempts <= max_attempts);

		if (id == kInvalidEntityId || entities_.find(id) != entities_.end()) {
			return nullptr;
		}
	}
	if (entities_.find(id) != entities_.end()) {
		return nullptr;  // ID collision
	}
	auto entity = std::make_unique<Entity>(id);
	entity->SetTimerManager(timer_mgr_);
	Entity* raw = entity.get();
	entities_[id] = std::move(entity);
	return raw;
}

Entity* EntityManager::GetEntity(EntityId id) {
	ENGINE_PROFILE_ENTITY_GET();
	auto it = entities_.find(id);
	return it != entities_.end() ? it->second.get() : nullptr;
}

void EntityManager::DestroyEntity(EntityId id) {
	ENGINE_PROFILE_ENTITY_DESTROY();
	auto it = entities_.find(id);
	if (it == entities_.end()) return;
	uint32_t body_id = it->second->GetPhysicsBodyId();
	if (body_id != Entity::kInvalidPhysicsBodyId) {
		UnregisterPhysicsBodyBinding(body_id);
	}
	NotifyEntityDestroying(*it->second);
	it->second->Destroy();
	entities_.erase(it);
}

void EntityManager::DestroyAll() {
	ENGINE_PROFILE_SCOPE("engine.entity", "DestroyAll");
	std::vector<EntityId> ids;
	ids.reserve(entities_.size());
	for (const auto& pair : entities_) {
		ids.push_back(pair.first);
	}
	for (EntityId id : ids) {
		DestroyEntity(id);
	}
	entities_.clear();
	conn_to_entity_.clear();
	body_to_entity_.clear();
}

Entity* EntityManager::FindByConnection(const evpp::TCPConnPtr& conn) {
	ENGINE_PROFILE_SCOPE("engine.entity", "FindByConnection");
	if (!conn) return nullptr;
	auto it = conn_to_entity_.find(conn.get());
	if (it == conn_to_entity_.end()) return nullptr;
	return GetEntity(it->second);
}

void EntityManager::RegisterConnectionBinding(const evpp::TCPConn* raw_conn, EntityId id) {
	if (!raw_conn || GetEntity(id) == nullptr) return;
	auto existing = conn_to_entity_.find(raw_conn);
	if (existing != conn_to_entity_.end() && existing->second != id) {
		if (auto* old_entity = GetEntity(existing->second)) {
			old_entity->ClearConnectionForManager(raw_conn);
		}
	}
	conn_to_entity_[raw_conn] = id;
}

void EntityManager::UnregisterConnectionBinding(const evpp::TCPConn* raw_conn) {
	if (!raw_conn) return;
	auto it = conn_to_entity_.find(raw_conn);
	if (it == conn_to_entity_.end()) return;
	if (auto* entity = GetEntity(it->second)) {
		entity->ClearConnectionForManager(raw_conn);
	}
	conn_to_entity_.erase(it);
}

void EntityManager::ForEachActive(std::function<void(Entity&)> callback) {
	ENGINE_PROFILE_SCOPE("engine.entity", "ForEachActive");
	std::vector<EntityId> ids;
	ids.reserve(entities_.size());
	for (const auto& pair : entities_) {
		if (pair.second->GetState() == EntityState::Active) {
			ids.push_back(pair.first);
		}
	}
	for (EntityId id : ids) {
		auto* entity = GetEntity(id);
		if (entity && entity->GetState() == EntityState::Active) {
			callback(*entity);
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

Entity* EntityManager::FindByPhysicsBodyId(uint32_t body_id) {
	auto it = body_to_entity_.find(body_id);
	if (it == body_to_entity_.end()) return nullptr;
	return GetEntity(it->second);
}

void EntityManager::RegisterPhysicsBodyBinding(uint32_t body_id, EntityId id) {
	if (body_id == Entity::kInvalidPhysicsBodyId) return;
	auto* entity = GetEntity(id);
	if (!entity) return;

	auto existing = body_to_entity_.find(body_id);
	if (existing != body_to_entity_.end() && existing->second != id) {
		if (auto* old_entity = GetEntity(existing->second)) {
			old_entity->ClearPhysicsBodyId();
		}
	}

	if (entity->HasPhysicsBody() && entity->GetPhysicsBodyId() != body_id) {
		body_to_entity_.erase(entity->GetPhysicsBodyId());
	}

	entity->SetPhysicsBodyId(body_id);
	body_to_entity_[body_id] = id;
}

void EntityManager::UnregisterPhysicsBodyBinding(uint32_t body_id) {
	auto it = body_to_entity_.find(body_id);
	if (it == body_to_entity_.end()) return;
	if (auto* entity = GetEntity(it->second)) {
		if (entity->GetPhysicsBodyId() == body_id) {
			entity->ClearPhysicsBodyId();
		}
	}
	body_to_entity_.erase(it);
}

}  // namespace entity
}  // namespace engine
