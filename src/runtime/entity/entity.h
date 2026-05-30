#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <runtime/evpp/tcp_callbacks.h>

#include "attribute.h"
#include "entity_id.h"
#include "runtime/core/engine_api.h"
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

class CLOUD_ENGINE_API Entity {
public:
	static constexpr uint32_t kInvalidPhysicsBodyId = UINT32_MAX;

	explicit Entity(EntityId id);
	~Entity();

	Entity(const Entity&) = delete;
	Entity& operator=(const Entity&) = delete;
	Entity(Entity&&) = delete;
	Entity& operator=(Entity&&) = delete;

	// Lifecycle
	EntityId GetId() const { return id_; }
	EntityState GetState() const { return state_; }
	bool IsCreated() const { return state_ == EntityState::Created; }
	bool IsActive() const { return state_ == EntityState::Active; }
	bool IsSuspended() const { return state_ == EntityState::Suspended; }
	bool IsDestroyed() const { return state_ == EntityState::Destroyed; }
	void Activate();
	void Suspend();
	void Destroy();  // cancels timers, unbinds connection, clears attrs/components

	// Attributes
	AttributeTable& Attrs() { return attrs_; }
	const AttributeTable& Attrs() const { return attrs_; }

	// Components (C++ type-erased via shared_ptr<void>)
	template <typename T>
	T* AddComponent(std::unique_ptr<T> component) {
		if (!component) return nullptr;
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
	bool HasComponent() const {
		return components_.find(std::type_index(typeid(T))) != components_.end();
	}

	template <typename T>
	void RemoveComponent() {
		components_.erase(std::type_index(typeid(T)));
	}

	// Lua-level named components (keyed by string name)
	void AddLuaComponent(const std::string& name, int lua_ref);
	int GetLuaComponent(const std::string& name) const;
	void RemoveLuaComponent(const std::string& name);
	std::unordered_map<std::string, int> TakeLuaComponents();
	void ClearLuaComponents();
	size_t LuaComponentCount() const { return lua_components_.size(); }
	size_t ComponentCount() const { return components_.size(); }

	// Connection binding
	void BindConnection(const evpp::TCPConnPtr& conn);
	void UnbindConnection();
	evpp::TCPConnPtr GetConnection() const { return connection_; }

	// Physics body linkage
	void SetPhysicsBodyId(uint32_t body_id) { physics_body_id_ = body_id; }
	void ClearPhysicsBodyId() { physics_body_id_ = kInvalidPhysicsBodyId; }
	uint32_t GetPhysicsBodyId() const { return physics_body_id_; }
	bool HasPhysicsBody() const { return physics_body_id_ != kInvalidPhysicsBodyId; }

	// TimerManager injection — set by EntityManager at creation time.
	void SetTimerManager(TimerManager* tm) { timer_mgr_ = tm; }
	TimerManager* GetTimerManager() const { return timer_mgr_; }

	// Timer ownership — timers are auto-cancelled on Destroy.
	// The callback should capture EntityId and check EntityManager for safety.
	void AddOwnedTimer(TimerId id);
	void RemoveOwnedTimer(TimerId id);
	size_t OwnedTimerCount() const { return owned_timers_.size(); }

	// Convenience: create a timer whose callback is guarded by entity existence.
	TimerId AddTimer(int64_t interval_ms, bool repeat, std::function<void()> callback);
	void CancelTimer(TimerId id);

private:
	friend class EntityManager;
	void SetState(EntityState state) { state_ = state; }
	void ClearConnectionForManager(const evpp::TCPConn* raw_conn);

	EntityId id_;
	EntityState state_ = EntityState::Created;
	AttributeTable attrs_;
	evpp::TCPConnPtr connection_;
	bool connection_registered_ = false;
	std::vector<TimerId> owned_timers_;
	TimerManager* timer_mgr_ = nullptr;

	// type_index → type-erased component
	std::unordered_map<std::type_index, std::shared_ptr<void>> components_;

	// Lua component name → registry ref
	std::unordered_map<std::string, int> lua_components_;

	uint32_t physics_body_id_ = kInvalidPhysicsBodyId;
};

}  // namespace entity
}  // namespace engine
