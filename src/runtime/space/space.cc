#include "runtime/space/space.h"

#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace space {

Space::Space(SpaceId id, const SpaceConfig& config)
	: id_(id)
	, config_(config)
	, id_allocator_(std::make_unique<entity::SequentialIdAllocator>()) {
	vm_ = std::make_unique<ScriptVM>();
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Space [{}]: created, name=[{}], max_entities=[{}]",
					id_, config_.name, config_.max_entities);
}

Space::~Space() {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Space [{}]: destroying, entity_count=[{}]",
					id_, entities_.size());

	// Suspend all player entities before teardown
	for (auto& [eid, conn] : player_connections_) {
		auto it = entities_.find(eid);
		if (it != entities_.end() && it->second->GetState() == entity::EntityState::Active) {
			it->second->Suspend();
		}
	}
	player_connections_.clear();
	entities_.clear();

	ENGINE_LOG_INFO(logger, "Space [{}]: destroyed", id_);
}

lua_State* Space::GetLuaState() {
	return vm_ ? vm_->GetState() : nullptr;
}

entity::Entity* Space::CreateEntity(entity::EntityId id) {
	if (entities_.size() >= config_.max_entities) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "Space [{}]: entity limit reached [{}]", id_, config_.max_entities);
		return nullptr;
	}

	if (id == entity::kInvalidEntityId) {
		id = id_allocator_->Allocate();
	}

	if (entities_.find(id) != entities_.end()) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "Space [{}]: entity [{}] already exists", id_, id);
		return nullptr;
	}

	auto entity = std::make_unique<entity::Entity>(id);
	auto* raw = entity.get();
	raw->Activate();
	entities_[id] = std::move(entity);
	return raw;
}

entity::Entity* Space::GetEntity(entity::EntityId id) {
	auto it = entities_.find(id);
	if (it == entities_.end()) return nullptr;
	return it->second.get();
}

void Space::DestroyEntity(entity::EntityId id) {
	auto it = entities_.find(id);
	if (it == entities_.end()) return;
	it->second->Destroy();
	player_connections_.erase(id);
	entities_.erase(it);
}

void Space::OnPlayerJoin(entity::EntityId player_id, evpp::TCPConnPtr conn) {
	auto* entity = GetEntity(player_id);
	if (!entity) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "Space [{}]: player join failed, entity [{}] not found",
						 id_, player_id);
		return;
	}
	entity->BindConnection(conn);
	player_connections_[player_id] = std::move(conn);
}

void Space::OnPlayerLeave(entity::EntityId player_id) {
	auto* entity = GetEntity(player_id);
	if (entity) {
		entity->Suspend();  // suspend, not destroy — enables reconnect
	}
	player_connections_.erase(player_id);
}

evpp::TCPConnPtr Space::GetPlayerConnection(entity::EntityId player_id) const {
	auto it = player_connections_.find(player_id);
	if (it == player_connections_.end()) return nullptr;
	return it->second;
}

void Space::Update(int64_t delta_ms) {
	if (!vm_) return;
	vm_->UpdateScript();
}

bool Space::LoadScripts(const std::vector<std::string>& script_paths) {
	if (!vm_) return false;

	auto* logger = GetLogger();
	for (const auto& path : script_paths) {
		if (!vm_->DoFile(path)) {
			ENGINE_LOG_ERROR(logger, "Space [{}]: failed to load script [{}]", id_, path);
			return false;
		}
	}

	vm_->InitScript();
	ENGINE_LOG_INFO(logger, "Space [{}]: loaded [{}] scripts", id_, script_paths.size());
	return true;
}

void Space::ForEachEntity(std::function<void(entity::Entity&)> callback) {
	for (auto& [id, entity] : entities_) {
		callback(*entity);
	}
}

}  // namespace space
}  // namespace engine
