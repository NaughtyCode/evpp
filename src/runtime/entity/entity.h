#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <runtime/evpp/tcp_callbacks.h>

#include "attribute.h"
#include "entity_id.h"
#include "runtime/core/timer/timer_manager.h"

namespace engine {
namespace entity {

enum class EntityState {
	Created,
	Active,
	Suspended,
	Destroyed,
};

class EntityManager;

class Entity {
public:
	explicit Entity(EntityId id);
	~Entity();

	Entity(const Entity&) = delete;
	Entity& operator=(const Entity&) = delete;
	Entity(Entity&&) = delete;
	Entity& operator=(Entity&&) = delete;

	// Lifecycle
	EntityId GetId() const { return id_; }
	EntityState GetState() const { return state_; }
	void Activate();
	void Suspend();
	void Destroy();  // cancels timers, unbinds connection, clears components

	// Attributes
	AttributeTable& Attrs() { return attrs_; }
	const AttributeTable& Attrs() const { return attrs_; }

	// Components (C++ type-erased via shared_ptr<void>)
	template <typename T>
	T* AddComponent(std::unique_ptr<T> component) {
		T* raw = component.get();
		components_[std::type_index(typeid(T))] = std::shared_ptr<void>(std::move(component));
		return raw;
	}

	template <typename T>
	T* GetComponent() {
		auto it = components_.find(std::type_index(typeid(T)));
		if (it == components_.end()) return nullptr;
		return static_cast<T*>(it->second.get());
	}

	template <typename T>
	void RemoveComponent() {
		components_.erase(std::type_index(typeid(T)));
	}

	// Lua-level named components (keyed by string name)
	void AddLuaComponent(const std::string& name, int lua_ref);
	int GetLuaComponent(const std::string& name) const;
	void RemoveLuaComponent(const std::string& name);

	// Connection binding
	void BindConnection(const evpp::TCPConnPtr& conn);
	void UnbindConnection();
	evpp::TCPConnPtr GetConnection() const { return connection_; }

	// Physics body linkage
	void SetPhysicsBodyId(uint32_t body_id) { physics_body_id_ = body_id; }
	uint32_t GetPhysicsBodyId() const { return physics_body_id_; }
	bool HasPhysicsBody() const { return physics_body_id_ != kInvalidBodyId; }

	// Timer ownership — timers are auto-cancelled on Destroy.
	// The callback should capture EntityId and check EntityManager for safety.
	void AddOwnedTimer(TimerId id);
	void RemoveOwnedTimer(TimerId id);

	// Convenience: create a timer whose callback is guarded by entity existence.
	TimerId AddTimer(int64_t interval_ms, bool repeat, std::function<void()> callback);
	void CancelTimer(TimerId id);

private:
	friend class EntityManager;
	void SetState(EntityState state) { state_ = state; }

	EntityId id_;
	EntityState state_ = EntityState::Created;
	AttributeTable attrs_;
	evpp::TCPConnPtr connection_;
	std::vector<TimerId> owned_timers_;

	// type_index → type-erased component
	std::unordered_map<std::type_index, std::shared_ptr<void>> components_;

	// Lua component name → registry ref
	std::unordered_map<std::string, int> lua_components_;

	static constexpr uint32_t kInvalidBodyId = UINT32_MAX;
	uint32_t physics_body_id_ = kInvalidBodyId;
};

}  // namespace entity
}  // namespace engine
