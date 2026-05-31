#include "runtime/space/space.h"

#include <atomic>

#include "runtime/core/log/log.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/space_bind.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace space {

namespace {

std::atomic<entity::EntityId> g_next_space_entity_id{1};

entity::EntityId AllocateSpaceEntityId() {
	for (;;) {
		auto id = g_next_space_entity_id.fetch_add(1, std::memory_order_relaxed);
		if (id != entity::kInvalidEntityId) {
			return id;
		}
	}
}

}  // namespace

Space::Space(SpaceId id, const SpaceConfig& config)
	: id_(id)
	, config_(config) {
	vm_ = std::make_unique<ScriptVM>();
	script::ExportSpace(*vm_, this);
	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "Space [{}]: created, name=[{}], max_entities=[{}]",
						id_, config_.name, config_.max_entities);
	}
}

Space::~Space() {
	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "Space [{}]: destroying, entity_count=[{}]",
						id_, entities_.size());
	}

	if (vm_) {
		vm_->DestroyScript();
	}

	// Suspend all player entities before teardown
	for (auto& [eid, conn] : player_connections_) {
		auto it = entities_.find(eid);
		if (it != entities_.end() && it->second->GetState() == entity::EntityState::Active) {
			it->second->Suspend();
		}
	}
	player_connections_.clear();
	entities_.clear();

	if (logger) {
		ENGINE_LOG_INFO(logger, "Space [{}]: destroyed", id_);
	}
}

lua_State* Space::GetLuaState() {
	return vm_ ? vm_->GetState() : nullptr;
}

entity::Entity* Space::CreateEntity(entity::EntityId id) {
	ENGINE_PROFILE_ENTITY_CREATE();
	if (entities_.size() >= config_.max_entities) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "Space [{}]: entity limit reached [{}]",
							 id_, config_.max_entities);
		}
		return nullptr;
	}

	if (id == entity::kInvalidEntityId) {
		size_t attempts = 0;
		const size_t max_attempts = entities_.size() + 1;
		do {
			id = AllocateSpaceEntityId();
			if (id != entity::kInvalidEntityId && entities_.find(id) == entities_.end()) {
				break;
			}
			++attempts;
		} while (attempts <= max_attempts);

		if (id == entity::kInvalidEntityId || entities_.find(id) != entities_.end()) {
			auto* logger = GetLogger();
			if (logger) {
				ENGINE_LOG_ERROR(logger, "Space [{}]: failed to allocate a free entity id", id_);
			}
			return nullptr;
		}
	}

	if (entities_.find(id) != entities_.end()) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "Space [{}]: entity [{}] already exists", id_, id);
		}
		return nullptr;
	}

	auto entity = std::make_unique<entity::Entity>(id);
	auto* raw = entity.get();
	raw->SetTimerManager(entity::EntityManager::Instance().GetTimerManager());
	raw->SetEntityResolver([this](entity::EntityId entity_id) {
		return GetEntity(entity_id);
	});
	entities_[id] = std::move(entity);
	return raw;
}

entity::Entity* Space::GetEntity(entity::EntityId id) {
	ENGINE_PROFILE_ENTITY_GET();
	auto it = entities_.find(id);
	if (it == entities_.end()) return nullptr;
	return it->second.get();
}

void Space::DestroyEntity(entity::EntityId id) {
	ENGINE_PROFILE_ENTITY_DESTROY();
	auto it = entities_.find(id);
	if (it == entities_.end()) return;
	it->second->Destroy();
	player_connections_.erase(id);
	entities_.erase(it);
}

bool Space::OnPlayerJoin(entity::EntityId player_id, evpp::TCPConnPtr conn) {
	ENGINE_PROFILE_SPACE_JOIN();
	auto* entity = GetEntity(player_id);
	if (!entity) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "Space [{}]: player join failed, entity [{}] not found",
							 id_, player_id);
		}
		return false;
	}

	if (player_connections_.find(player_id) == player_connections_.end() &&
		player_connections_.size() >= config_.max_players) {
		auto* logger = GetLogger();
		if (logger) {
			ENGINE_LOG_ERROR(logger, "Space [{}]: player limit reached [{}]", id_,
							 config_.max_players);
		}
		return false;
	}

	entity->BindConnection(conn);
	entity->Activate();
	player_connections_[player_id] = std::move(conn);
	return true;
}

void Space::OnPlayerLeave(entity::EntityId player_id) {
	ENGINE_PROFILE_SPACE_LEAVE();
	auto* entity = GetEntity(player_id);
	if (entity) {
		entity->UnbindConnection();
		entity->Suspend();  // suspend, not destroy: enables reconnect
	}
	player_connections_.erase(player_id);
}

evpp::TCPConnPtr Space::GetPlayerConnection(entity::EntityId player_id) const {
	auto it = player_connections_.find(player_id);
	if (it == player_connections_.end()) return nullptr;
	return it->second;
}

void Space::Update(int64_t delta_ms) {
	ENGINE_PROFILE_SPACE_UPDATE();
	if (!vm_) return;
	vm_->UpdateScript();
}

bool Space::LoadScripts(const std::vector<std::string>& script_paths, std::string* error_out) {
	ENGINE_PROFILE_SPACE_LOAD_SCRIPTS();
	if (!vm_) {
		if (error_out) *error_out = "ScriptVM not initialized";
		return false;
	}

	auto* logger = GetLogger();
	for (const auto& path : script_paths) {
		std::string error;
		if (!vm_->DoFile(path, &error)) {
			if (logger) {
				ENGINE_LOG_ERROR(logger, "Space [{}]: failed to load script [{}]", id_, path);
			}
			if (error_out) {
				*error_out = error.empty() ? "failed to load script: " + path
										   : "failed to load script " + path + ": " + error;
			}
			return false;
		}
	}

	vm_->InitScript();
	if (logger) {
		ENGINE_LOG_INFO(logger, "Space [{}]: loaded [{}] scripts", id_, script_paths.size());
	}
	return true;
}

void Space::ForEachEntity(std::function<void(entity::Entity&)> callback) {
	std::vector<entity::EntityId> ids;
	ids.reserve(entities_.size());
	for (const auto& [id, entity] : entities_) {
		ids.push_back(id);
	}

	for (auto id : ids) {
		auto it = entities_.find(id);
		if (it != entities_.end()) {
			callback(*it->second);
		}
	}
}

}  // namespace space
}  // namespace engine
