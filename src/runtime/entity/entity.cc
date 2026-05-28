#include "entity.h"

#include <algorithm>
#include <chrono>

#include "entity_manager.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/profiler/profiler_events.h"

using namespace std::chrono;

namespace engine {
namespace entity {

Entity::Entity(EntityId id) : id_(id) {
	attrs_.SetOwnerId(id);
}

Entity::~Entity() {
	Destroy();
}

void Entity::Activate() {
	ENGINE_PROFILE_ENTITY_ACTIVATE();
	if (state_ == EntityState::Created || state_ == EntityState::Suspended) {
		state_ = EntityState::Active;
	}
}

void Entity::Suspend() {
	ENGINE_PROFILE_ENTITY_SUSPEND();
	if (state_ == EntityState::Active) {
		state_ = EntityState::Suspended;
	}
}

void Entity::Destroy() {
	ENGINE_PROFILE_ENTITY_DESTROY();
	if (state_ == EntityState::Destroyed) return;
	state_ = EntityState::Destroyed;

	for (auto tid : owned_timers_) {
		if (timer_mgr_) timer_mgr_->cancel_timer(tid);
	}
	owned_timers_.clear();

	UnbindConnection();
	lua_components_.clear();
}

void Entity::AddLuaComponent(const std::string& name, int lua_ref) {
	lua_components_[name] = lua_ref;
}

int Entity::GetLuaComponent(const std::string& name) const {
	auto it = lua_components_.find(name);
	return it != lua_components_.end() ? it->second : -1;
}

void Entity::RemoveLuaComponent(const std::string& name) {
	lua_components_.erase(name);
}

void Entity::BindConnection(const evpp::TCPConnPtr& conn) {
	ENGINE_PROFILE_SCOPE("engine.entity", "BindConnection");
	UnbindConnection();
	connection_ = conn;
	if (conn) {
		EntityManager::Instance().RegisterConnectionBinding(conn.get(), id_);
	}
}

void Entity::UnbindConnection() {
	ENGINE_PROFILE_SCOPE("engine.entity", "UnbindConnection");
	if (connection_) {
		EntityManager::Instance().UnregisterConnectionBinding(connection_.get());
		connection_.reset();
	}
}

void Entity::AddOwnedTimer(TimerId id) {
	owned_timers_.push_back(id);
}

void Entity::RemoveOwnedTimer(TimerId id) {
	auto it = std::find(owned_timers_.begin(), owned_timers_.end(), id);
	if (it != owned_timers_.end()) {
		owned_timers_.erase(it);
	}
}

TimerId Entity::AddTimer(int64_t interval_ms, bool repeat, std::function<void()> callback) {
	ENGINE_PROFILE_SCOPE("engine.entity", "AddTimer");
	EntityId eid = id_;
	auto safe_cb = [eid, cb = std::move(callback)]() {
		auto* entity = EntityManager::Instance().GetEntity(eid);
		if (entity && entity->GetState() == EntityState::Active) {
			cb();
		}
	};

	TimerId tid;
	if (repeat) {
		tid = timer_mgr_->create_repeating_simple_timer(
			milliseconds(interval_ms), std::move(safe_cb));
		timer_mgr_->start_timer_relative(tid, milliseconds(interval_ms));
	} else {
		tid = timer_mgr_->create_timer_for(
			milliseconds(interval_ms), std::move(safe_cb));
	}
	owned_timers_.push_back(tid);
	return tid;
}

void Entity::CancelTimer(TimerId id) {
	auto it = std::find(owned_timers_.begin(), owned_timers_.end(), id);
	if (it != owned_timers_.end()) {
		timer_mgr_->cancel_timer(id);
		owned_timers_.erase(it);
	}
}

}  // namespace entity
}  // namespace engine
