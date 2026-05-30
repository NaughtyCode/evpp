#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <runtime/evpp/tcp_callbacks.h>

#include "runtime/core/engine_api.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"

namespace engine {

class ScriptVM;

}  // namespace engine

struct lua_State;

namespace engine {
namespace space {

using SpaceId = uint64_t;
inline constexpr SpaceId kInvalidSpaceId = 0;

struct SpaceConfig {
	std::string name;
	std::vector<std::string> entry_scripts;
	size_t max_entities = 10000;
	size_t max_players = 1000;
};

// Space — isolated game world with its own Lua VM and entity set

class CLOUD_ENGINE_API Space {
public:
	explicit Space(SpaceId id, const SpaceConfig& config);
	~Space();

	Space(const Space&) = delete;
	Space& operator=(const Space&) = delete;

	SpaceId GetId() const { return id_; }
	const std::string& GetName() const { return config_.name; }
	lua_State* GetLuaState();
	ScriptVM& GetScriptVM() { return *vm_; }

	// Entity management within this space
	entity::Entity* CreateEntity(entity::EntityId id = 0);
	entity::Entity* GetEntity(entity::EntityId id);
	void DestroyEntity(entity::EntityId id);
	size_t EntityCount() const { return entities_.size(); }

	// Player join/leave — connection → entity lifecycle
	bool OnPlayerJoin(entity::EntityId player_id, evpp::TCPConnPtr conn);
	void OnPlayerLeave(entity::EntityId player_id);
	evpp::TCPConnPtr GetPlayerConnection(entity::EntityId player_id) const;
	size_t PlayerCount() const { return player_connections_.size(); }

	// Per-frame update
	void Update(int64_t delta_ms);

	// Script loading
	bool LoadScripts(const std::vector<std::string>& script_paths,
					 std::string* error_out = nullptr);

	// Iteration
	void ForEachEntity(std::function<void(entity::Entity&)> callback);

private:
	SpaceId id_;
	SpaceConfig config_;
	std::unique_ptr<ScriptVM> vm_;
	std::unordered_map<entity::EntityId, std::unique_ptr<entity::Entity>> entities_;
	std::unordered_map<entity::EntityId, evpp::TCPConnPtr> player_connections_;
};

}  // namespace space
}  // namespace engine
