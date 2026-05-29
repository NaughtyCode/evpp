#include "runtime/config/config_bind.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

// ── helpers ────────────────────────────────────────────────────────────

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

// ── config.get(path) ───────────────────────────────────────────────────

int l_config_get(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string p(path);

    auto& cfg = ConfigManager::Instance();
    auto rt = cfg.GetRuntimeConfig();
    auto srv = cfg.GetServerConfig();

    // RuntimeConfig — top-level
    if (p == "resource_dir")           return PushConfigValue(L, rt.resource_dir);
    if (p == "scripts_dir")            return PushConfigValue(L, rt.scripts_dir);
    if (p == "sandbox_level")          return PushConfigValue(L, rt.sandbox_level);
    if (p == "environment")            return PushConfigValue(L, rt.environment);

    // RuntimeConfig — log.*
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

    // RuntimeConfig — frame.*
    if (p == "frame.target_fps")                 return PushConfigValue(L, rt.frame.target_fps);
    if (p == "frame.interval_ms")                return PushConfigValue(L, rt.frame.interval_ms);
    if (p == "frame.slow_threshold_multiplier")  return PushConfigValue(L, rt.frame.slow_threshold_multiplier);

    // ServerConfig
    if (p == "server.http.timeout_sec")       return PushConfigValue(L, srv.http.timeout_sec);
    if (p == "server.scripts_dir")            return PushConfigValue(L, srv.scripts_dir);
    if (p == "server.admin_port")             return PushConfigValue(L, srv.admin_port);
    if (p == "server.admin_bind_address")     return PushConfigValue(L, srv.admin_bind_address);
    if (p == "server.shutdown_timeout_sec")   return PushConfigValue(L, srv.shutdown_timeout_sec);
    if (p == "server.connection_drain_timeout_sec") return PushConfigValue(L, srv.connection_drain_timeout_sec);
    if (p == "server.max_connections")        return PushConfigValue(L, srv.max_connections);
    if (p == "server.pid_file")               return PushConfigValue(L, srv.pid_file);
    if (p == "server.active_mongodb")         return PushConfigValue(L, srv.active_mongodb);
    if (p == "server.db_service")             return PushConfigValue(L, srv.db_service);
    if (p == "server.mongodb_dev")            return PushConfigValue(L, srv.mongodb_dev);
    if (p == "server.mongodb_public")         return PushConfigValue(L, srv.mongodb_public);
    if (p == "server.msgpack.max_nesting_depth") return PushConfigValue(L, srv.msgpack.max_nesting_depth);
    if (p == "server.msgpack.max_payload_size")  return PushConfigValue(L, static_cast<lua_Integer>(srv.msgpack.max_payload_size));

    lua_pushnil(L);
    return 1;
}

// ── config.get_module(name) ────────────────────────────────────────────

// Load a JSON array-of-objects file and push it as a Lua array-of-tables.
// Each object in the array becomes a Lua table with key-value pairs.
// The resulting table is indexed by row number (1-based) AND by the "id"
// field if present.

void PushJsonValue(lua_State* L, const std::string& json, size_t& pos);

void SkipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
           json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
}

std::string ReadJsonString(const std::string& json, size_t& pos) {
    std::string result;
    ++pos;  // skip opening "
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\') {
            ++pos;
            if (pos < json.size()) {
                switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += json[pos]; break;
                }
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    if (pos < json.size()) ++pos;  // skip closing "
    return result;
}

double ReadJsonNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    if (json[pos] == '-') ++pos;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) ++pos;
    if (pos < json.size() && json[pos] == '.') {
        ++pos;
        while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) ++pos;
        return std::stod(json.substr(start, pos - start));
    }
    return static_cast<double>(std::stoll(json.substr(start, pos - start)));
}

bool ReadJsonBool(const std::string& json, size_t& pos) {
    if (json.compare(pos, 4, "true") == 0) { pos += 4; return true; }
    if (json.compare(pos, 5, "false") == 0) { pos += 5; return false; }
    return false;
}

void PushJsonValue(lua_State* L, const std::string& json, size_t& pos) {
    SkipWhitespace(json, pos);
    if (pos >= json.size()) { lua_pushnil(L); return; }

    char c = json[pos];
    if (c == '"') {
        lua_pushstring(L, ReadJsonString(json, pos).c_str());
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        double num = ReadJsonNumber(json, pos);
        double intpart;
        if (std::modf(num, &intpart) == 0.0 && num >= std::numeric_limits<lua_Integer>::min() &&
            num <= std::numeric_limits<lua_Integer>::max()) {
            lua_pushinteger(L, static_cast<lua_Integer>(num));
        } else {
            lua_pushnumber(L, num);
        }
    } else if (c == 't' || c == 'f') {
        lua_pushboolean(L, ReadJsonBool(json, pos) ? 1 : 0);
    } else if (c == '{') {
        ++pos;  // skip '{'
        lua_newtable(L);
        int idx = 1;
        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '"') {
            // Object
            while (pos < json.size() && json[pos] != '}') {
                SkipWhitespace(json, pos);
                if (json[pos] == '}') break;
                std::string key = ReadJsonString(json, pos);
                SkipWhitespace(json, pos);
                if (json[pos] == ':') ++pos;
                PushJsonValue(L, json, pos);
                lua_setfield(L, -2, key.c_str());
                SkipWhitespace(json, pos);
                if (json[pos] == ',') ++pos;
            }
        } else {
            // Array
            while (pos < json.size() && json[pos] != '}') {
                PushJsonValue(L, json, pos);
                lua_rawseti(L, -2, idx++);
                SkipWhitespace(json, pos);
                if (json[pos] == ',') ++pos;
            }
        }
        if (pos < json.size()) ++pos;  // skip '}'
    } else if (c == '[') {
        ++pos;  // skip '['
        lua_newtable(L);
        int idx = 1;
        while (pos < json.size() && json[pos] != ']') {
            PushJsonValue(L, json, pos);
            lua_rawseti(L, -2, idx++);
            SkipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (pos < json.size()) ++pos;  // skip ']'
    } else if (c == 'n') {
        if (json.compare(pos, 4, "null") == 0) { pos += 4; lua_pushnil(L); return; }
        lua_pushnil(L);
    } else {
        lua_pushnil(L);
    }
}

int l_config_get_module(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    auto& cfg = ConfigManager::Instance();
    auto rt = cfg.GetRuntimeConfig();

    std::string path = rt.resource_dir + "/script/data/" + name + ".json";

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        lua_pushnil(L);
        lua_pushfstring(L, "module '%s' not found at %s", name, path.c_str());
        return 2;
    }

    std::stringstream buf;
    buf << ifs.rdbuf();
    std::string json = buf.str();

    // Expect top-level array of objects
    size_t pos = 0;
    SkipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '[') {
        lua_pushnil(L);
        lua_pushstring(L, "module JSON must be a top-level array");
        return 2;
    }

    ++pos;  // skip '['
    lua_newtable(L);
    int row_idx = 1;

    while (pos < json.size()) {
        SkipWhitespace(json, pos);
        if (json[pos] == ']') break;
        if (json[pos] != '{') {
            // Skip unexpected token
            lua_pushnil(L);
            lua_pushfstring(L, "expected object in array at position %zu", pos);
            return 2;
        }

        ++pos;  // skip '{'
        lua_newtable(L);
        bool has_id = false;
        lua_Integer id_val = 0;

        while (pos < json.size()) {
            SkipWhitespace(json, pos);
            if (json[pos] == '}') break;
            std::string key = ReadJsonString(json, pos);
            SkipWhitespace(json, pos);
            if (json[pos] == ':') ++pos;
            PushJsonValue(L, json, pos);
            lua_setfield(L, -2, key.c_str());

            // Track the "id" field for indexing
            if (key == "id" && lua_isinteger(L, -1)) {
                has_id = true;
                id_val = lua_tointeger(L, -1);
            }
            // Actually track id by checking after setfield
            lua_getfield(L, -1, "id");
            if (!lua_isnil(L, -1) && !has_id) {
                if (lua_isinteger(L, -1)) {
                    has_id = true;
                    id_val = lua_tointeger(L, -1);
                }
            }
            lua_pop(L, 1);

            SkipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (pos < json.size()) ++pos;  // skip '}'

        // Row index (1-based)
        lua_pushvalue(L, -1);  // duplicate the row table
        lua_rawseti(L, -3, row_idx++);

        // ID index if present
        if (has_id) {
            lua_pushvalue(L, -1);
            lua_rawseti(L, -3, static_cast<int>(id_val));
        }

        SkipWhitespace(json, pos);
        if (json[pos] == ',') ++pos;
    }

    return 1;
}

// ── config.on_change(module, callback) ─────────────────────────────────

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
    ConfigChangeSet changes;
};

struct ChangeCallbackEntry {
    int callback_ref;       // Lua registry reference
    int reload_cb_id;       // ConfigManager callback id
    lua_State* L;           // Owning Lua state (only accessed from main thread)
    std::string module;     // The module name to filter on
};

// Global state — callbacks survive across config reloads.
static std::vector<ChangeCallbackEntry> g_change_callbacks;
static std::mutex g_change_cb_mutex;

// Per-lua_State pending event queues.
static std::unordered_map<lua_State*, std::vector<PendingConfigEvent>> g_pending_events;
static std::mutex g_pending_mutex;

// Enqueue a pending callback event. Called from any thread.
static void EnqueuePendingEvent(lua_State* L, int cb_ref,
                                const ConfigChangeSet& changes) {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending_events[L].push_back({cb_ref, changes});
}

int l_config_on_change(lua_State* L) {
    const char* module = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // Create a registry reference to the Lua callback
    lua_pushvalue(L, 2);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Register a C++ reload callback with ConfigManager.
    // The callback only enqueues pending events — it does NOT call Lua
    // directly, because it may fire from the FileWatcher thread.
    auto& cfg = ConfigManager::Instance();
    int cb_id = cfg.RegisterReloadCallback(
        [L, cb_ref](const ConfigChangeSet& changes) {
            EnqueuePendingEvent(L, cb_ref, changes);
        });

    std::lock_guard<std::mutex> lock(g_change_cb_mutex);
    g_change_callbacks.push_back({cb_ref, cb_id, nullptr, module});

    // Return the callback ID so Lua can unregister it later
    lua_pushinteger(L, cb_id);
    return 1;
}

// ── config.unregister(id) ──────────────────────────────────────────────

int l_config_unregister(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    auto& cfg = ConfigManager::Instance();
    cfg.UnregisterReloadCallback(id);

    std::lock_guard<std::mutex> lock(g_change_cb_mutex);
    g_change_callbacks.erase(
        std::remove_if(g_change_callbacks.begin(), g_change_callbacks.end(),
                       [id](const ChangeCallbackEntry& e) { return e.reload_cb_id == id; }),
        g_change_callbacks.end());
    return 0;
}

// ── config.flush_changes() ───────────────────────────────────────────────

// Forward declaration — the implementation is FlushConfigCallbacks below.
int FlushConfigCallbacks(lua_State* L);

int l_config_flush_changes(lua_State* L) {
    int count = FlushConfigCallbacks(L);
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

// ── Module registration table ──────────────────────────────────────────

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

}  // namespace

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
    for (const auto& event : pending) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, event.callback_ref);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, event.callback_ref);
            continue;
        }

        lua_newtable(L);
        for (size_t i = 0; i < event.changes.size(); ++i) {
            lua_newtable(L);
            lua_pushstring(L, event.changes[i].field_path.c_str());
            lua_setfield(L, -2, "field");
            lua_pushstring(L, event.changes[i].old_value.c_str());
            lua_setfield(L, -2, "old_value");
            lua_pushstring(L, event.changes[i].new_value.c_str());
            lua_setfield(L, -2, "new_value");
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }

        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            if (logger) ENGINE_LOG_ERROR(logger, "config.on_change callback error: {}", err);
            lua_pop(L, 1);
        }
    }

    return static_cast<int>(pending.size());
}

}  // namespace script
}  // namespace engine
