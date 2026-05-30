#include "entity.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include "entity_manager.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/profiler/profiler_events.h"

using namespace std::chrono;

namespace engine {
namespace entity {

namespace {

constexpr int64_t kMaxTimerIntervalMs = INT64_MAX / kNsPerMs;

}  // namespace

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
	EntityManager::Instance().NotifyEntityDestroying(*this);

	auto timers = std::move(owned_timers_);
	owned_timers_.clear();
	if (timer_mgr_) {
		for (auto tid : timers) {
			timer_mgr_->destroy_timer(tid);
		}
	}

	UnbindConnection();
	if (HasPhysicsBody()) {
		EntityManager::Instance().UnregisterPhysicsBodyBinding(physics_body_id_);
	}
	components_.clear();
	ClearLuaComponents();
	ClearPhysicsBodyId();
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

std::unordered_map<std::string, int> Entity::TakeLuaComponents() {
	std::unordered_map<std::string, int> refs;
	refs.swap(lua_components_);
	return refs;
}

void Entity::ClearLuaComponents() {
	lua_components_.clear();
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
	}
	connection_.reset();
}

void Entity::ClearConnectionForManager(const evpp::TCPConn* raw_conn) {
	if (!raw_conn || connection_.get() == raw_conn) {
		connection_.reset();
	}
}

void Entity::AddOwnedTimer(TimerId id) {
	if (id == kInvalidTimerId) return;
	if (std::find(owned_timers_.begin(), owned_timers_.end(), id) != owned_timers_.end()) {
		return;
	}
	owned_timers_.push_back(id);
}

void Entity::RemoveOwnedTimer(TimerId id) {
	owned_timers_.erase(std::remove(owned_timers_.begin(), owned_timers_.end(), id),
						owned_timers_.end());
}

TimerId Entity::AddTimer(int64_t interval_ms, bool repeat, std::function<void()> callback) {
	ENGINE_PROFILE_SCOPE("engine.entity", "AddTimer");
	if (!timer_mgr_ || !timer_mgr_->is_initialized()) {
		return kInvalidTimerId;
	}
	if (state_ == EntityState::Destroyed || interval_ms <= 0 ||
		interval_ms > kMaxTimerIntervalMs || !callback) {
		return kInvalidTimerId;
	}

	EntityId eid = id_;
	auto tid_holder = std::make_shared<TimerId>(kInvalidTimerId);
	TimerManager* timer_mgr = timer_mgr_;
	auto safe_cb = [eid, repeat, tid_holder, timer_mgr, cb = std::move(callback)]() {
		auto* entity = EntityManager::Instance().GetEntity(eid);
		if (entity && !repeat) {
			entity->RemoveOwnedTimer(*tid_holder);
		}
		if (entity && entity->GetState() == EntityState::Active) {
			cb();
		}
		if (!repeat && timer_mgr && *tid_holder != kInvalidTimerId) {
			timer_mgr->destroy_timer(*tid_holder);
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
	if (tid == kInvalidTimerId) {
		return kInvalidTimerId;
	}
	*tid_holder = tid;
	AddOwnedTimer(tid);
	return tid;
}

void Entity::CancelTimer(TimerId id) {
	auto it = std::find(owned_timers_.begin(), owned_timers_.end(), id);
	if (it != owned_timers_.end()) {
		if (timer_mgr_) {
			timer_mgr_->destroy_timer(id);
		}
		owned_timers_.erase(it);
	}
}

}  // namespace entity
}  // namespace engine
