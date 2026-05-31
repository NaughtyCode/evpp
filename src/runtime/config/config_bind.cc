#include "runtime/config/config_bind.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glaze/json.hpp>

#include "runtime/config/config.h"
#include "runtime/config/path_resolver.h"
#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

// helpers

int PushConfigValue(lua_State* L, int val) {
    lua_pushinteger(L, val);
    return 1;
}

int PushConfigValue(lua_State* L, double val) {
    lua_pushnumber(L, val);
    return 1;
}

int PushConfigValue(lua_State* L, const std::string& val) {
    lua_pushstring(L, val.c_str());
    return 1;
}

int PushConfigValue(lua_State* L, bool val) {
    lua_pushboolean(L, val ? 1 : 0);
    return 1;
}

int PushConfigValue(lua_State* L, lua_Integer val) {
    lua_pushinteger(L, val);
    return 1;
}

int PushConfigValue(lua_State* L, size_t val) {
    lua_pushinteger(L, static_cast<lua_Integer>(val));
    return 1;
}

// config.get(path)

int l_config_get(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string p(path);

    auto& cfg = ConfigManager::Instance();
    auto rt = cfg.GetRuntimeConfig();
    auto srv = cfg.GetServerConfig();

    // RuntimeConfig - top-level
    if (p == "resource_dir")           return PushConfigValue(L, rt.resource_dir);
    if (p == "scripts_dir")            return PushConfigValue(L, rt.scripts_dir);
    if (p == "sandbox_level")          return PushConfigValue(L, rt.sandbox_level);
    if (p == "environment")            return PushConfigValue(L, rt.environment);

    // RuntimeConfig - log.*
    if (p == "log.dir")                return PushConfigValue(L, rt.log.dir);
    if (p == "log.level")              return PushConfigValue(L, rt.log.level);
    if (p == "log.rotation_size_mb")   return PushConfigValue(L, rt.log.rotation_size_mb);
    if (p == "log.max_backup_files")   return PushConfigValue(L, rt.log.max_backup_files);
    if (p == "log.format_pattern")     return PushConfigValue(L, rt.log.format_pattern);
    if (p == "log.rotation_frequency") return PushConfigValue(L, rt.log.rotation_frequency);
    if (p == "log.rotation_interval")  return PushConfigValue(L, rt.log.rotation_interval);
    if (p == "log.rotation_time_daily") return PushConfigValue(L, rt.log.rotation_time_daily);
    if (p == "log.rotation_naming_scheme") return PushConfigValue(L, rt.log.rotation_naming_scheme);
    if (p == "log.logger_name")        return PushConfigValue(L, rt.log.logger_name);
    if (p == "log.log_filename")       return PushConfigValue(L, rt.log.log_filename);

    // RuntimeConfig - frame.*
    if (p == "frame.target_fps")                 return PushConfigValue(L, rt.frame.target_fps);
    if (p == "frame.interval_ms")                return PushConfigValue(L, rt.frame.interval_ms);
    if (p == "frame.slow_threshold_multiplier")  return PushConfigValue(L, rt.frame.slow_threshold_multiplier);

    // ServerConfig
    if (p == "server.http.timeout_sec")       return PushConfigValue(L, srv.http.timeout_sec);
    if (p == "server.scripts_dir")            return PushConfigValue(L, srv.scripts_dir);
    if (p == "server.admin_port")             return PushConfigValue(L, srv.admin_port);
    if (p == "server.admin_bind_address")     return PushConfigValue(L, srv.admin_bind_address);
    if (p == "server.admin_metrics_enabled")  return PushConfigValue(L, srv.admin_metrics_enabled);
    if (p == "server.shutdown_timeout_sec")   return PushConfigValue(L, srv.shutdown_timeout_sec);
    if (p == "server.connection_drain_timeout_sec") return PushConfigValue(L, srv.connection_drain_timeout_sec);
    if (p == "server.max_connections")        return PushConfigValue(L, srv.max_connections);
    if (p == "server.pid_file")               return PushConfigValue(L, srv.pid_file);
    if (p == "server.active_mongodb")         return PushConfigValue(L, srv.active_mongodb);
    if (p == "server.db_service")             return PushConfigValue(L, srv.db_service);
    if (p == "server.mongodb_dev")            return PushConfigValue(L, srv.mongodb_dev);
    if (p == "server.mongodb_public")         return PushConfigValue(L, srv.mongodb_public);
    if (p == "server.db_required")            return PushConfigValue(L, srv.db_required);
    if (p == "server.msgpack.max_nesting_depth") return PushConfigValue(L, srv.msgpack.max_nesting_depth);
    if (p == "server.msgpack.max_payload_size")  return PushConfigValue(L, srv.msgpack.max_payload_size);
    if (p == "server.resource_limits.max_message_size") return PushConfigValue(L, static_cast<lua_Integer>(srv.resource_limits.max_message_size));
    if (p == "server.resource_limits.max_buffer_capacity") return PushConfigValue(L, static_cast<lua_Integer>(srv.resource_limits.max_buffer_capacity));
    if (p == "server.resource_limits.max_http_body_size") return PushConfigValue(L, static_cast<lua_Integer>(srv.resource_limits.max_http_body_size));
    if (p == "server.resource_limits.max_msgpack_depth") return PushConfigValue(L, static_cast<lua_Integer>(srv.resource_limits.max_msgpack_depth));
    if (p == "server.tcp_keepalive.idle_sec") return PushConfigValue(L, srv.tcp_keepalive.idle_sec);
    if (p == "server.tcp_keepalive.interval_sec") return PushConfigValue(L, srv.tcp_keepalive.interval_sec);
    if (p == "server.tcp_keepalive.count") return PushConfigValue(L, srv.tcp_keepalive.count);
    if (p == "server.instance.id") return PushConfigValue(L, srv.instance.id);
    if (p == "server.instance.region") return PushConfigValue(L, srv.instance.region);
    if (p == "server.instance.zone") return PushConfigValue(L, srv.instance.zone);
    if (p == "server.instance.cluster") return PushConfigValue(L, srv.instance.cluster);

    lua_pushnil(L);
    return 1;
}

// config.get_module(name)

// Load a JSON array-of-objects file and push it as a Lua array-of-tables.
// Each object in the array becomes a Lua table with key-value pairs.
// The resulting table is indexed by row number (1-based) AND by the "id"
// field if present.

using ModuleJsonValue = glz::generic_i64;
using ModuleJsonArray = ModuleJsonValue::array_t;
using ModuleJsonObject = ModuleJsonValue::object_t;

constexpr int kMaxConfigModuleDepth = 256;

bool PushModuleJsonValue(lua_State* L,
                         const ModuleJsonValue& value,
                         std::string& error,
                         int depth);

void BuildModuleIdIndex(lua_State* L) {
    if (!lua_istable(L, -1)) {
        return;
    }

    const lua_Integer row_count = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= row_count; ++i) {
        lua_rawgeti(L, -1, static_cast<int>(i));
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        lua_getfield(L, -1, "id");
        if (lua_isinteger(L, -1)) {
            const lua_Integer id = lua_tointeger(L, -1);
            lua_pop(L, 1);
            lua_pushvalue(L, -1);
            lua_rawseti(L, -3, static_cast<int>(id));
        } else {
            lua_pop(L, 1);
        }

        lua_pop(L, 1);
    }
}

bool PushModuleJsonArray(lua_State* L,
                         const ModuleJsonArray& array,
                         std::string& error,
                         int depth) {
    if (array.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = "module JSON array is too large for Lua";
        return false;
    }
    if (!lua_checkstack(L, 2)) {
        error = "Lua stack overflow while pushing module array";
        return false;
    }

    lua_createtable(L, static_cast<int>(array.size()), 0);
    for (size_t i = 0; i < array.size(); ++i) {
        if (!PushModuleJsonValue(L, array[i], error, depth + 1)) {
            return false;
        }
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return true;
}

bool PushModuleJsonObject(lua_State* L,
                          const ModuleJsonObject& object,
                          std::string& error,
                          int depth) {
    if (object.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = "module JSON object is too large for Lua";
        return false;
    }
    if (!lua_checkstack(L, 3)) {
        error = "Lua stack overflow while pushing module object";
        return false;
    }

    lua_createtable(L, 0, static_cast<int>(object.size()));
    for (const auto& [key, child] : object) {
        lua_pushlstring(L, key.data(), key.size());
        if (!PushModuleJsonValue(L, child, error, depth + 1)) {
            return false;
        }
        lua_settable(L, -3);
    }
    return true;
}

bool PushModuleJsonValue(lua_State* L,
                         const ModuleJsonValue& value,
                         std::string& error,
                         int depth) {
    if (depth > kMaxConfigModuleDepth) {
        error = "module JSON maximum nesting depth exceeded";
        return false;
    }

    if (value.is_null()) {
        lua_pushnil(L);
        return true;
    }
    if (value.is_boolean()) {
        lua_pushboolean(L, value.get_boolean() ? 1 : 0);
        return true;
    }
    if (value.is_string()) {
        const auto& string = value.get_string();
        lua_pushlstring(L, string.data(), string.size());
        return true;
    }
    if (value.is_int64()) {
        lua_pushinteger(L, static_cast<lua_Integer>(value.get<int64_t>()));
        return true;
    }
    if (value.is_double()) {
        lua_pushnumber(L, static_cast<lua_Number>(value.get<double>()));
        return true;
    }
    if (value.is_array()) {
        return PushModuleJsonArray(L, value.get_array(), error, depth);
    }
    if (value.is_object()) {
        return PushModuleJsonObject(L, value.get_object(), error, depth);
    }

    error = "module JSON contains an unsupported value";
    return false;
}

int l_config_get_module(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    auto& cfg = ConfigManager::Instance();
    auto rt = cfg.GetRuntimeConfig();

    const auto path = config::ResolvePathFromWorkingTree(
        std::filesystem::path(rt.resource_dir) / "script" / "data" / (std::string(name) + ".json"));

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        lua_pushnil(L);
        const auto path_string = path.string();
        lua_pushfstring(L, "module '%s' not found at %s", name, path_string.c_str());
        return 2;
    }

    std::stringstream buf;
    buf << ifs.rdbuf();
    std::string json = buf.str();

    auto parsed = glz::read_json<ModuleJsonValue>(json);
    if (!parsed) {
        lua_pushnil(L);
        const auto message = std::string("module JSON parse error: ") +
                             glz::format_error(parsed.error(), json);
        lua_pushlstring(L, message.data(), message.size());
        return 2;
    }
    if (!parsed->is_array()) {
        lua_pushnil(L);
        lua_pushstring(L, "module JSON must be a top-level array");
        return 2;
    }

    std::string error;
    if (!PushModuleJsonValue(L, *parsed, error, 0)) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }
    BuildModuleIdIndex(L);

    return 1;
}

// config.on_change(module, callback)

// Per-VM storage for on_change callbacks.
// Each entry holds a Lua function reference (via luaL_ref in the registry).
//
// Thread safety: the ConfigManager reload callback may fire from any thread
// (main thread or FileWatcher thread). Lua API calls must only happen on the
// VM's owning thread. To handle this, the reload callback enqueues change
// sets into a pending queue; the caller is responsible for calling
// FlushConfigCallbacks() from the VM's main thread (typically at the top of
// the event-loop frame or from ScriptVM::OnFrame).

struct PendingConfigEvent {
	int callback_ref;
	std::string module;  // callback module filter at registration time
	ConfigChangeSet changes;
};

struct ChangeCallbackEntry {
	int callback_ref;       // Lua registry reference
	int reload_cb_id;       // ConfigManager callback id
	lua_State* L;           // Owning Lua state (only accessed from main thread)
	std::string module;     // The module name to filter on
};

// Global state - callbacks survive across config reloads.
static std::vector<ChangeCallbackEntry> g_change_callbacks;
static std::mutex g_change_cb_mutex;

// Per-lua_State pending event queues.
static std::unordered_map<lua_State*, std::vector<PendingConfigEvent>> g_pending_events;
static std::mutex g_pending_mutex;

// Module filter helper: callback receives a change only when it touches
// the requested module path. Empty string or "*" means "all modules".
static bool MatchesModule(const std::string& module, const std::string& field_path) {
	if (module.empty() || module == "*") return true;
	if (module == field_path) return true;
	return field_path.size() > module.size() &&
		field_path.compare(0, module.size(), module) == 0 &&
		field_path[module.size()] == '.';
}

// Build a module-filtered changeset for callback dispatch.
static ConfigChangeSet FilterChangesByModule(const std::string& module,
										   const ConfigChangeSet& changes) {
	if (module.empty() || module == "*") return changes;

	ConfigChangeSet filtered;
	filtered.reserve(changes.size());
	for (const auto& entry : changes) {
		if (MatchesModule(module, entry.field_path)) {
			filtered.push_back(entry);
		}
	}
	return filtered;
}

static void RemovePendingEventsForCallback(lua_State* L, int callback_ref) {
	if (!L) return;
	std::lock_guard<std::mutex> lock(g_pending_mutex);
	auto it = g_pending_events.find(L);
	if (it == g_pending_events.end()) return;
	auto& queue = it->second;
	queue.erase(std::remove_if(queue.begin(), queue.end(),
							 [callback_ref](const PendingConfigEvent& event) {
								 return event.callback_ref == callback_ref;
							 }),
				queue.end());
	if (queue.empty()) {
		g_pending_events.erase(it);
	}
}

// Enqueue a pending callback event. Called from any thread.
static void EnqueuePendingEvent(lua_State* L, int cb_ref,
                                const std::string& module,
                                const ConfigChangeSet& changes) {
	std::lock_guard<std::mutex> lock(g_pending_mutex);
	g_pending_events[L].push_back({cb_ref, module, changes});
}

int l_config_on_change(lua_State* L) {
    const char* module = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // Create a registry reference to the Lua callback.
    lua_pushvalue(L, 2);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    std::string module_name(module);
    auto& cfg = ConfigManager::Instance();
    int cb_id = cfg.RegisterReloadCallback([L, cb_ref, module_name](const ConfigChangeSet& changes) {
        EnqueuePendingEvent(L, cb_ref, module_name, changes);
    });

    std::lock_guard<std::mutex> lock(g_change_cb_mutex);
    g_change_callbacks.push_back({cb_ref, cb_id, L, module_name});

    // Return the callback ID so Lua can unregister it later.
    lua_pushinteger(L, cb_id);
    return 1;
}

int l_config_unregister(lua_State* L) {
    const int id = static_cast<int>(luaL_checkinteger(L, 1));

    std::vector<ChangeCallbackEntry> removed;
    {
        std::lock_guard<std::mutex> lock(g_change_cb_mutex);
        auto it = g_change_callbacks.begin();
        while (it != g_change_callbacks.end()) {
            if (it->reload_cb_id == id && it->L == L) {
                removed.push_back(*it);
                it = g_change_callbacks.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto& cfg = ConfigManager::Instance();
    for (const auto& entry : removed) {
        cfg.UnregisterReloadCallback(entry.reload_cb_id);
        RemovePendingEventsForCallback(entry.L, entry.callback_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, entry.callback_ref);
    }
    return 0;
}

// config.flush_changes()

int l_config_flush_changes(lua_State* L) {
    int count = FlushConfigCallbacks(L);
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

// Module registration table

const luaL_Reg kConfigFunctions[] = {
    {"get",            l_config_get},
    {"get_module",     l_config_get_module},
    {"on_change",      l_config_on_change},
    {"unregister",     l_config_unregister},
    {"flush_changes",  l_config_flush_changes},
    {nullptr, nullptr},
};

}  // namespace

void ExportConfigBindings(ScriptVM& vm) {
    vm.RegisterModule("config", kConfigFunctions);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger,
        "ScriptBind: config module exported (config.get/get_module/on_change/flush_changes)");
}

int FlushConfigCallbacks(lua_State* L) {
    std::vector<PendingConfigEvent> pending;
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        auto it = g_pending_events.find(L);
        if (it != g_pending_events.end()) {
            pending = std::move(it->second);
            g_pending_events.erase(it);
        }
    }

    auto* logger = GetLogger();
    int fired = 0;
    for (const auto& event : pending) {
        auto filtered_changes = FilterChangesByModule(event.module, event.changes);
        if (filtered_changes.empty()) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, event.callback_ref);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, event.callback_ref);
            continue;
        }

        lua_newtable(L);
        for (size_t i = 0; i < filtered_changes.size(); ++i) {
            lua_newtable(L);
            lua_pushstring(L, filtered_changes[i].field_path.c_str());
            lua_setfield(L, -2, "field");
            lua_pushstring(L, filtered_changes[i].old_value.c_str());
            lua_setfield(L, -2, "old_value");
            lua_pushstring(L, filtered_changes[i].new_value.c_str());
            lua_setfield(L, -2, "new_value");
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }

        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (logger) ENGINE_LOG_ERROR(logger, "config.on_change callback error: {}", err);
            lua_pop(L, 1);
        } else {
            ++fired;
        }
    }

    return fired;
}

void ShutdownConfigBindings(lua_State* L) {
    if (!L) return;

    std::vector<ChangeCallbackEntry> removed;
    {
        std::lock_guard<std::mutex> lock(g_change_cb_mutex);
        auto it = g_change_callbacks.begin();
        while (it != g_change_callbacks.end()) {
            if (it->L == L) {
                removed.push_back(*it);
                it = g_change_callbacks.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto& cfg = ConfigManager::Instance();
    for (const auto& entry : removed) {
        cfg.UnregisterReloadCallback(entry.reload_cb_id);
        RemovePendingEventsForCallback(entry.L, entry.callback_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, entry.callback_ref);
    }
}

}  // namespace script
}  // namespace engine
