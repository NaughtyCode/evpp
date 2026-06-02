/*
 * client_runtime.cpp - C ABI wrappers for runtime subsystems beyond
 * lifecycle/network/timer: config, JSON, msgpack, auth, metrics, entity,
 * space, and AOI.
 */

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include "client_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <glaze/json.hpp>

#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/auth/auth_backend.h"
#include "runtime/auth/session_manager.h"
#include "runtime/config/config.h"
#include "runtime/engine/engine.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/monitoring/metrics.h"
#include "runtime/space/space_manager.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

struct game_aoi_s {
    std::unique_ptr<engine::aoi::AOIManager> manager;
};

namespace {

using engine::ConfigManager;

void clear_buffer(char* buf, int cap) {
    if (buf && cap > 0) {
        buf[0] = '\0';
    }
}

game_error_t fail(game_client_t* client, game_error_t code, const std::string& msg) {
    set_error(client, msg.c_str());
    return code;
}

game_error_t require_client(game_client_t* client) {
    if (!client) return GAME_ERR_INVALID_ARG;
    return GAME_OK;
}

game_error_t copy_text(game_client_t* client,
                       const std::string& value,
                       char* out_buf,
                       int out_cap,
                       int* out_len,
                       const char* label) {
    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return fail(client, GAME_ERR_OUT_OF_MEMORY, std::string(label) + " is too large");
    }

    const int required = static_cast<int>(value.size());
    if (out_len) {
        *out_len = required;
    }

    if (!out_buf || out_cap == 0) {
        clear_error(client);
        return GAME_OK;
    }
    if (out_cap < 0) {
        return fail(client, GAME_ERR_INVALID_ARG, "negative output buffer size");
    }

    const size_t cap = static_cast<size_t>(out_cap);
    const size_t copied = cap > 0 ? std::min(value.size(), cap - 1) : 0;
    if (copied > 0) {
        std::memcpy(out_buf, value.data(), copied);
    }
    if (cap > 0) {
        out_buf[copied] = '\0';
    }

    if (out_cap <= required) {
        return fail(client, GAME_ERR_BUFFER_TOO_SMALL, std::string(label) + " buffer too small");
    }

    clear_error(client);
    return GAME_OK;
}

game_error_t copy_binary(game_client_t* client,
                         const char* data,
                         size_t data_len,
                         uint8_t* out_buf,
                         int out_cap,
                         int* out_len,
                         const char* label) {
    if (data_len > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return fail(client, GAME_ERR_OUT_OF_MEMORY, std::string(label) + " is too large");
    }

    const int required = static_cast<int>(data_len);
    if (out_len) {
        *out_len = required;
    }

    if (!out_buf || out_cap == 0) {
        clear_error(client);
        return GAME_OK;
    }
    if (out_cap < 0) {
        return fail(client, GAME_ERR_INVALID_ARG, "negative output buffer size");
    }
    if (out_cap < required) {
        return fail(client, GAME_ERR_BUFFER_TOO_SMALL, std::string(label) + " buffer too small");
    }

    if (data_len > 0) {
        std::memcpy(out_buf, data, data_len);
    }
    clear_error(client);
    return GAME_OK;
}

void copy_fixed_string(char* out_buf, int out_cap, const std::string& value) {
    if (!out_buf || out_cap <= 0) return;
    const size_t cap = static_cast<size_t>(out_cap);
    const size_t copied = std::min(value.size(), cap - 1);
    if (copied > 0) {
        std::memcpy(out_buf, value.data(), copied);
    }
    out_buf[copied] = '\0';
}

std::string glaze_error(const char* prefix, const glz::error_ctx& ec, const std::string& input) {
    return std::string(prefix) + ": " + glz::format_error(ec, input);
}

lua_State* get_lua_state_for_client(game_client_t* client) {
    if (!client || !client->initialized) return nullptr;
    try {
        return engine::Engine::Instance().GetScriptVM().GetState();
    } catch (...) {
        return nullptr;
    }
}

std::string lua_value_to_string(lua_State* L, int index) {
    const int abs_index = lua_absindex(L, index);
    switch (lua_type(L, abs_index)) {
    case LUA_TNIL:
    case LUA_TNONE:
        return {};
    case LUA_TBOOLEAN:
        return lua_toboolean(L, abs_index) ? "true" : "false";
    case LUA_TNUMBER:
        if (lua_isinteger(L, abs_index)) {
            return std::to_string(static_cast<long long>(lua_tointeger(L, abs_index)));
        } else {
            std::ostringstream oss;
            oss << lua_tonumber(L, abs_index);
            return oss.str();
        }
    case LUA_TSTRING: {
        size_t len = 0;
        const char* str = lua_tolstring(L, abs_index, &len);
        return std::string(str ? str : "", len);
    }
    case LUA_TTABLE: {
        const int top = lua_gettop(L);
        lua_getglobal(L, "json");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "encode");
            lua_remove(L, -2);
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, abs_index);
                if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
                    size_t len = 0;
                    const char* str = lua_tolstring(L, -1, &len);
                    std::string result(str ? str : "", len);
                    lua_settop(L, top);
                    return result;
                }
            }
        }
        lua_settop(L, top);
        return "table";
    }
    default:
        return lua_typename(L, lua_type(L, abs_index));
    }
}

struct ConfigScalar {
    enum class Type { None, String, Int, Double, Bool };

    Type type = Type::None;
    std::string string_value;
    int64_t int_value = 0;
    double double_value = 0.0;
    bool bool_value = false;

    static ConfigScalar String(std::string value) {
        ConfigScalar scalar;
        scalar.type = Type::String;
        scalar.string_value = std::move(value);
        return scalar;
    }

    static ConfigScalar Int(int64_t value) {
        ConfigScalar scalar;
        scalar.type = Type::Int;
        scalar.int_value = value;
        return scalar;
    }

    static ConfigScalar Size(size_t value) {
        const auto max = static_cast<size_t>((std::numeric_limits<int64_t>::max)());
        return Int(static_cast<int64_t>(std::min(value, max)));
    }

    static ConfigScalar Double(double value) {
        ConfigScalar scalar;
        scalar.type = Type::Double;
        scalar.double_value = value;
        return scalar;
    }

    static ConfigScalar Bool(bool value) {
        ConfigScalar scalar;
        scalar.type = Type::Bool;
        scalar.bool_value = value;
        return scalar;
    }
};

bool lookup_config_scalar(const std::string& path, ConfigScalar& out) {
    const auto rt = ConfigManager::Instance().GetRuntimeConfig();
    const auto cc = ConfigManager::Instance().GetClientConfig();
    const auto srv = ConfigManager::Instance().GetServerConfig();

    const auto is = [&](const char* bare, const char* scoped) {
        return path == bare || path == scoped;
    };

    if (is("resource_dir", "runtime.resource_dir")) { out = ConfigScalar::String(rt.resource_dir); return true; }
    if (is("scripts_dir", "runtime.scripts_dir")) { out = ConfigScalar::String(rt.scripts_dir); return true; }
    if (is("sandbox_level", "runtime.sandbox_level")) { out = ConfigScalar::String(rt.sandbox_level); return true; }
    if (is("environment", "runtime.environment")) { out = ConfigScalar::String(rt.environment); return true; }
    if (is("log.dir", "runtime.log.dir")) { out = ConfigScalar::String(rt.log.dir); return true; }
    if (is("log.level", "runtime.log.level")) { out = ConfigScalar::String(rt.log.level); return true; }
    if (is("log.rotation_size_mb", "runtime.log.rotation_size_mb")) { out = ConfigScalar::Int(rt.log.rotation_size_mb); return true; }
    if (is("log.max_backup_files", "runtime.log.max_backup_files")) { out = ConfigScalar::Int(rt.log.max_backup_files); return true; }
    if (is("log.format_pattern", "runtime.log.format_pattern")) { out = ConfigScalar::String(rt.log.format_pattern); return true; }
    if (is("log.rotation_frequency", "runtime.log.rotation_frequency")) { out = ConfigScalar::String(rt.log.rotation_frequency); return true; }
    if (is("log.rotation_interval", "runtime.log.rotation_interval")) { out = ConfigScalar::Int(rt.log.rotation_interval); return true; }
    if (is("log.rotation_time_daily", "runtime.log.rotation_time_daily")) { out = ConfigScalar::String(rt.log.rotation_time_daily); return true; }
    if (is("log.rotation_naming_scheme", "runtime.log.rotation_naming_scheme")) { out = ConfigScalar::String(rt.log.rotation_naming_scheme); return true; }
    if (is("log.logger_name", "runtime.log.logger_name")) { out = ConfigScalar::String(rt.log.logger_name); return true; }
    if (is("log.log_filename", "runtime.log.log_filename")) { out = ConfigScalar::String(rt.log.log_filename); return true; }
    if (is("frame.target_fps", "runtime.frame.target_fps")) { out = ConfigScalar::Int(rt.frame.target_fps); return true; }
    if (is("frame.interval_ms", "runtime.frame.interval_ms")) { out = ConfigScalar::Int(rt.frame.interval_ms); return true; }
    if (is("frame.slow_threshold_multiplier", "runtime.frame.slow_threshold_multiplier")) { out = ConfigScalar::Int(rt.frame.slow_threshold_multiplier); return true; }
    if (is("hot_reload.enabled", "runtime.hot_reload.enabled")) { out = ConfigScalar::Bool(rt.hot_reload.enabled); return true; }
    if (is("hot_reload.startup_delay_ms", "runtime.hot_reload.startup_delay_ms")) { out = ConfigScalar::Int(rt.hot_reload.startup_delay_ms); return true; }
    if (is("hot_reload.poll_interval_ms", "runtime.hot_reload.poll_interval_ms")) { out = ConfigScalar::Int(rt.hot_reload.poll_interval_ms); return true; }
    if (is("hot_reload.debounce_ms", "runtime.hot_reload.debounce_ms")) { out = ConfigScalar::Int(rt.hot_reload.debounce_ms); return true; }

    if (path == "client.scripts_dir") { out = ConfigScalar::String(cc.scripts_dir); return true; }
    if (path == "client.render.backend") { out = ConfigScalar::String(cc.render.backend); return true; }
    if (path == "client.render.resolution_width") { out = ConfigScalar::Int(cc.render.resolution_width); return true; }
    if (path == "client.render.resolution_height") { out = ConfigScalar::Int(cc.render.resolution_height); return true; }
    if (path == "client.render.fullscreen") { out = ConfigScalar::Bool(cc.render.fullscreen); return true; }
    if (path == "client.render.vsync") { out = ConfigScalar::Bool(cc.render.vsync); return true; }
    if (path == "client.render.msaa_samples") { out = ConfigScalar::Int(cc.render.msaa_samples); return true; }
    if (path == "client.render.hdr") { out = ConfigScalar::Bool(cc.render.hdr); return true; }
    if (path == "client.render.max_fps") { out = ConfigScalar::Int(cc.render.max_fps); return true; }
    if (path == "client.window.title") { out = ConfigScalar::String(cc.window.title); return true; }
    if (path == "client.window.width") { out = ConfigScalar::Int(cc.window.width); return true; }
    if (path == "client.window.height") { out = ConfigScalar::Int(cc.window.height); return true; }
    if (path == "client.window.resizable") { out = ConfigScalar::Bool(cc.window.resizable); return true; }
    if (path == "client.window.borderless") { out = ConfigScalar::Bool(cc.window.borderless); return true; }
    if (path == "client.window.monitor") { out = ConfigScalar::Int(cc.window.monitor); return true; }
    if (path == "client.input.mouse_sensitivity") { out = ConfigScalar::Double(cc.input.mouse_sensitivity); return true; }
    if (path == "client.input.mouse_invert_y") { out = ConfigScalar::Bool(cc.input.mouse_invert_y); return true; }
    if (path == "client.input.gamepad_deadzone") { out = ConfigScalar::Double(cc.input.gamepad_deadzone); return true; }
    if (path == "client.input.touch_enabled") { out = ConfigScalar::Bool(cc.input.touch_enabled); return true; }
    if (path == "client.audio.backend") { out = ConfigScalar::String(cc.audio.backend); return true; }
    if (path == "client.audio.sample_rate") { out = ConfigScalar::Int(cc.audio.sample_rate); return true; }
    if (path == "client.audio.channels") { out = ConfigScalar::Int(cc.audio.channels); return true; }
    if (path == "client.audio.master_volume") { out = ConfigScalar::Double(cc.audio.master_volume); return true; }
    if (path == "client.audio.music_volume") { out = ConfigScalar::Double(cc.audio.music_volume); return true; }
    if (path == "client.audio.sfx_volume") { out = ConfigScalar::Double(cc.audio.sfx_volume); return true; }
    if (path == "client.audio.spatial_audio") { out = ConfigScalar::Bool(cc.audio.spatial_audio); return true; }
    if (path == "client.audio.mute_when_unfocused") { out = ConfigScalar::Bool(cc.audio.mute_when_unfocused); return true; }
    if (path == "client.network.server_address") { out = ConfigScalar::String(cc.network.server_address); return true; }
    if (path == "client.network.server_port") { out = ConfigScalar::Int(cc.network.server_port); return true; }
    if (path == "client.network.reconnect_max_retries") { out = ConfigScalar::Int(cc.network.reconnect_max_retries); return true; }
    if (path == "client.network.reconnect_base_delay_ms") { out = ConfigScalar::Int(cc.network.reconnect_base_delay_ms); return true; }
    if (path == "client.network.reconnect_max_delay_ms") { out = ConfigScalar::Int(cc.network.reconnect_max_delay_ms); return true; }
    if (path == "client.network.timeout_ms") { out = ConfigScalar::Int(cc.network.timeout_ms); return true; }
    if (path == "client.network.client_prediction") { out = ConfigScalar::Bool(cc.network.client_prediction); return true; }
    if (path == "client.network.interpolation_delay_ms") { out = ConfigScalar::Int(cc.network.interpolation_delay_ms); return true; }
    if (path == "client.assets.root_path") { out = ConfigScalar::String(cc.assets.root_path); return true; }
    if (path == "client.assets.streaming_budget_mb") { out = ConfigScalar::Int(cc.assets.streaming_budget_mb); return true; }
    if (path == "client.assets.lod_bias") { out = ConfigScalar::Double(cc.assets.lod_bias); return true; }
    if (path == "client.assets.texture_quality") { out = ConfigScalar::String(cc.assets.texture_quality); return true; }
    if (path == "client.ui.font_path") { out = ConfigScalar::String(cc.ui.font_path); return true; }
    if (path == "client.ui.font_size") { out = ConfigScalar::Int(cc.ui.font_size); return true; }
    if (path == "client.ui.scale") { out = ConfigScalar::Double(cc.ui.scale); return true; }
    if (path == "client.ui.locale") { out = ConfigScalar::String(cc.ui.locale); return true; }
    if (path == "client.ui.theme") { out = ConfigScalar::String(cc.ui.theme); return true; }
    if (path == "client.ui.color_blind_mode") { out = ConfigScalar::String(cc.ui.color_blind_mode); return true; }
    if (path == "client.platform.save_data_path") { out = ConfigScalar::String(cc.platform.save_data_path); return true; }
    if (path == "client.platform.cache_path") { out = ConfigScalar::String(cc.platform.cache_path); return true; }
    if (path == "client.platform.locale") { out = ConfigScalar::String(cc.platform.locale); return true; }
    if (path == "client.first_run_completed") { out = ConfigScalar::Bool(cc.first_run_completed); return true; }

    if (path == "server.http.timeout_sec") { out = ConfigScalar::Double(srv.http.timeout_sec); return true; }
    if (path == "server.scripts_dir") { out = ConfigScalar::String(srv.scripts_dir); return true; }
    if (path == "server.admin_port") { out = ConfigScalar::Int(srv.admin_port); return true; }
    if (path == "server.admin_bind_address") { out = ConfigScalar::String(srv.admin_bind_address); return true; }
    if (path == "server.admin_metrics_enabled") { out = ConfigScalar::Bool(srv.admin_metrics_enabled); return true; }
    if (path == "server.shutdown_timeout_sec") { out = ConfigScalar::Int(srv.shutdown_timeout_sec); return true; }
    if (path == "server.connection_drain_timeout_sec") { out = ConfigScalar::Int(srv.connection_drain_timeout_sec); return true; }
    if (path == "server.max_connections") { out = ConfigScalar::Int(srv.max_connections); return true; }
    if (path == "server.pid_file") { out = ConfigScalar::String(srv.pid_file); return true; }
    if (path == "server.active_mongodb") { out = ConfigScalar::String(srv.active_mongodb); return true; }
    if (path == "server.db_service") { out = ConfigScalar::String(srv.db_service); return true; }
    if (path == "server.mongodb_dev") { out = ConfigScalar::String(srv.mongodb_dev); return true; }
    if (path == "server.mongodb_public") { out = ConfigScalar::String(srv.mongodb_public); return true; }
    if (path == "server.db_required") { out = ConfigScalar::Bool(srv.db_required); return true; }
    if (path == "server.msgpack.max_nesting_depth") { out = ConfigScalar::Int(srv.msgpack.max_nesting_depth); return true; }
    if (path == "server.msgpack.max_payload_size") { out = ConfigScalar::Size(srv.msgpack.max_payload_size); return true; }
    if (path == "server.resource_limits.max_message_size") { out = ConfigScalar::Size(srv.resource_limits.max_message_size); return true; }
    if (path == "server.resource_limits.max_buffer_capacity") { out = ConfigScalar::Size(srv.resource_limits.max_buffer_capacity); return true; }
    if (path == "server.resource_limits.max_http_body_size") { out = ConfigScalar::Size(srv.resource_limits.max_http_body_size); return true; }
    if (path == "server.resource_limits.max_msgpack_depth") { out = ConfigScalar::Size(srv.resource_limits.max_msgpack_depth); return true; }
    if (path == "server.tcp_keepalive.idle_sec") { out = ConfigScalar::Int(srv.tcp_keepalive.idle_sec); return true; }
    if (path == "server.tcp_keepalive.interval_sec") { out = ConfigScalar::Int(srv.tcp_keepalive.interval_sec); return true; }
    if (path == "server.tcp_keepalive.count") { out = ConfigScalar::Int(srv.tcp_keepalive.count); return true; }
    if (path == "server.instance.id") { out = ConfigScalar::String(srv.instance.id); return true; }
    if (path == "server.instance.region") { out = ConfigScalar::String(srv.instance.region); return true; }
    if (path == "server.instance.zone") { out = ConfigScalar::String(srv.instance.zone); return true; }
    if (path == "server.instance.cluster") { out = ConfigScalar::String(srv.instance.cluster); return true; }

    return false;
}

std::string scalar_to_string(const ConfigScalar& scalar) {
    switch (scalar.type) {
    case ConfigScalar::Type::String:
        return scalar.string_value;
    case ConfigScalar::Type::Int:
        return std::to_string(static_cast<long long>(scalar.int_value));
    case ConfigScalar::Type::Double: {
        std::ostringstream oss;
        oss << scalar.double_value;
        return oss.str();
    }
    case ConfigScalar::Type::Bool:
        return scalar.bool_value ? "true" : "false";
    default:
        return {};
    }
}

std::shared_ptr<engine::auth::TokenAuthBackend> ensure_token_backend() {
    auto& manager = engine::auth::SessionManager::Instance();
    if (auto token = std::dynamic_pointer_cast<engine::auth::TokenAuthBackend>(
            manager.GetBackendSnapshot())) {
        return token;
    }

    auto backend = std::make_unique<engine::auth::TokenAuthBackend>();
    manager.SetBackend(std::move(backend));
    return std::dynamic_pointer_cast<engine::auth::TokenAuthBackend>(
        manager.GetBackendSnapshot());
}

std::shared_ptr<engine::auth::AuthBackend> ensure_auth_backend() {
    auto& manager = engine::auth::SessionManager::Instance();
    if (auto backend = manager.GetBackendSnapshot()) {
        return backend;
    }
    return ensure_token_backend();
}

engine::entity::Entity* find_entity(uint64_t entity_id) {
    if (entity_id == engine::entity::kInvalidEntityId) return nullptr;
    return engine::entity::EntityManager::Instance().GetEntity(
        static_cast<engine::entity::EntityId>(entity_id));
}

std::string attr_to_string(const engine::entity::AttrValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(static_cast<long long>(v));
        } else {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }
    }, value);
}

bool valid_positive_float(float value) {
    return std::isfinite(value) && value > 0.0f;
}

bool valid_finite_float(float value) {
    return std::isfinite(value);
}

game_error_t aoi_exception_to_error() {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return GAME_ERR_OUT_OF_MEMORY;
    } catch (const std::invalid_argument&) {
        return GAME_ERR_INVALID_ARG;
    } catch (...) {
        return GAME_ERR_GENERIC;
    }
}

game_error_t copy_entity_ids(const std::vector<engine::entity::EntityId>& ids,
                             uint64_t* out_ids,
                             int out_cap,
                             int* out_count) {
    if (ids.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return GAME_ERR_OUT_OF_MEMORY;
    }

    const int count = static_cast<int>(ids.size());
    if (out_cap < 0) {
        return GAME_ERR_INVALID_ARG;
    }
    if (!out_count && (!out_ids || out_cap == 0)) {
        return GAME_ERR_INVALID_ARG;
    }
    if (out_count) {
        *out_count = count;
    }
    if (!out_ids || out_cap == 0) {
        return GAME_OK;
    }
    if (out_cap < count) {
        return GAME_ERR_BUFFER_TOO_SMALL;
    }

    for (int i = 0; i < count; ++i) {
        out_ids[i] = static_cast<uint64_t>(ids[static_cast<size_t>(i)]);
    }
    return GAME_OK;
}

}  // namespace

extern "C" game_error_t game_client_eval_string(game_client_t* client,
                                                 const char* script,
                                                 char* out_buf,
                                                 int out_cap,
                                                 int* out_len,
                                                 char* error_out,
                                                 int error_size) {
    if (!client || !client->initialized || !script) return GAME_ERR_INVALID_ARG;

    lua_State* L = get_lua_state_for_client(client);
    if (!L) {
        const std::string error = "ScriptVM not available";
        set_error(client, error.c_str());
        copy_fixed_string(error_out, error_size, error);
        return GAME_ERR_SCRIPT;
    }

    const int base = lua_gettop(L);
    if (luaL_loadstring(L, script) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        const std::string error = err ? err : "Lua load error";
        lua_settop(L, base);
        set_error(client, error.c_str());
        copy_fixed_string(error_out, error_size, error);
        return GAME_ERR_SCRIPT;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        const std::string error = err ? err : "Lua runtime error";
        lua_settop(L, base);
        set_error(client, error.c_str());
        copy_fixed_string(error_out, error_size, error);
        return GAME_ERR_SCRIPT;
    }

    const int result_count = lua_gettop(L) - base;
    std::string value;
    if (result_count > 0) {
        value = lua_value_to_string(L, base + 1);
    }
    lua_settop(L, base);
    clear_buffer(error_out, error_size);
    return copy_text(client, value, out_buf, out_cap, out_len, "eval result");
}

extern "C" game_error_t game_config_load_runtime_json(game_client_t* client,
                                                       const char* json) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    if (!ConfigManager::Instance().LoadRuntimeFromString(json)) {
        return fail(client, GAME_ERR_SCRIPT, "failed to load runtime config JSON");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_load_client_json(game_client_t* client,
                                                      const char* json) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    if (!ConfigManager::Instance().LoadClientFromString(json)) {
        return fail(client, GAME_ERR_SCRIPT, "failed to load client config JSON");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_apply_client_overrides_json(
    game_client_t* client, const char* json) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    if (!ConfigManager::Instance().ApplyClientOverridesFromString(json)) {
        return fail(client, GAME_ERR_SCRIPT, "failed to apply client config overrides JSON");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_load_server_json(game_client_t* client,
                                                      const char* json) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    if (!ConfigManager::Instance().LoadServerFromString(json)) {
        return fail(client, GAME_ERR_SCRIPT, "failed to load server config JSON");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_reload(game_client_t* client,
                                            const char* config_dir) {
    if (!client || !config_dir || !*config_dir) return GAME_ERR_INVALID_ARG;
    if (!ConfigManager::Instance().Reload(config_dir)) {
        return fail(client, GAME_ERR_SCRIPT, "failed to reload config directory");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" bool game_config_validate(game_client_t* client,
                                      char* errors_buf, int errors_cap,
                                      char* warnings_buf, int warnings_cap) {
    if (!client) return false;
    const auto result = ConfigManager::Instance().Validate();
    copy_fixed_string(errors_buf, errors_cap, result.errors);
    copy_fixed_string(warnings_buf, warnings_cap, result.warnings);
    if (!result.valid) {
        set_error(client, result.errors.empty() ? "config validation failed" : result.errors.c_str());
    } else {
        clear_error(client);
    }
    return result.valid;
}

extern "C" game_error_t game_config_dump(game_client_t* client,
                                          char* out_buf,
                                          int out_cap,
                                          int* out_len) {
    if (require_client(client) != GAME_OK) return GAME_ERR_INVALID_ARG;
    return copy_text(client, ConfigManager::Instance().Dump(), out_buf, out_cap, out_len, "config dump");
}

extern "C" game_error_t game_config_get_string(game_client_t* client,
                                                const char* path,
                                                char* out_buf,
                                                int out_cap,
                                                int* out_len) {
    if (!client || !path) return GAME_ERR_INVALID_ARG;
    ConfigScalar scalar;
    if (!lookup_config_scalar(path, scalar)) {
        return fail(client, GAME_ERR_NOT_FOUND, std::string("config path not found: ") + path);
    }
    return copy_text(client, scalar_to_string(scalar), out_buf, out_cap, out_len, "config value");
}

extern "C" game_error_t game_config_get_int(game_client_t* client,
                                             const char* path,
                                             int64_t* out_value) {
    if (!client || !path || !out_value) return GAME_ERR_INVALID_ARG;
    ConfigScalar scalar;
    if (!lookup_config_scalar(path, scalar)) {
        return fail(client, GAME_ERR_NOT_FOUND, std::string("config path not found: ") + path);
    }
    if (scalar.type == ConfigScalar::Type::Int) {
        *out_value = scalar.int_value;
    } else if (scalar.type == ConfigScalar::Type::Bool) {
        *out_value = scalar.bool_value ? 1 : 0;
    } else {
        return fail(client, GAME_ERR_INVALID_ARG, std::string("config path is not an integer: ") + path);
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_get_double(game_client_t* client,
                                                const char* path,
                                                double* out_value) {
    if (!client || !path || !out_value) return GAME_ERR_INVALID_ARG;
    ConfigScalar scalar;
    if (!lookup_config_scalar(path, scalar)) {
        return fail(client, GAME_ERR_NOT_FOUND, std::string("config path not found: ") + path);
    }
    if (scalar.type == ConfigScalar::Type::Double) {
        *out_value = scalar.double_value;
    } else if (scalar.type == ConfigScalar::Type::Int) {
        *out_value = static_cast<double>(scalar.int_value);
    } else {
        return fail(client, GAME_ERR_INVALID_ARG, std::string("config path is not numeric: ") + path);
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_config_get_bool(game_client_t* client,
                                              const char* path,
                                              bool* out_value) {
    if (!client || !path || !out_value) return GAME_ERR_INVALID_ARG;
    ConfigScalar scalar;
    if (!lookup_config_scalar(path, scalar)) {
        return fail(client, GAME_ERR_NOT_FOUND, std::string("config path not found: ") + path);
    }
    if (scalar.type != ConfigScalar::Type::Bool) {
        return fail(client, GAME_ERR_INVALID_ARG, std::string("config path is not boolean: ") + path);
    }
    *out_value = scalar.bool_value;
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_json_validate(game_client_t* client,
                                            const char* json,
                                            bool allow_comments,
                                            char* error_out,
                                            int error_size) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    std::string input(json);
    const glz::error_ctx ec = allow_comments ? glz::validate_jsonc(input) : glz::validate_json(input);
    if (ec) {
        const std::string error = glaze_error("json validate", ec, input);
        set_error(client, error.c_str());
        copy_fixed_string(error_out, error_size, error);
        return GAME_ERR_SCRIPT;
    }
    clear_buffer(error_out, error_size);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_json_minify(game_client_t* client,
                                          const char* json,
                                          bool allow_comments,
                                          char* out_buf,
                                          int out_cap,
                                          int* out_len) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    std::string input(json);
    const glz::error_ctx ec = allow_comments ? glz::validate_jsonc(input) : glz::validate_json(input);
    if (ec) {
        return fail(client, GAME_ERR_SCRIPT, glaze_error("json minify", ec, input));
    }
    std::string output = allow_comments ? glz::minify_jsonc(input) : glz::minify_json(input);
    return copy_text(client, output, out_buf, out_cap, out_len, "json minify output");
}

extern "C" game_error_t game_json_prettify(game_client_t* client,
                                            const char* json,
                                            bool allow_comments,
                                            char* out_buf,
                                            int out_cap,
                                            int* out_len) {
    if (!client || !json) return GAME_ERR_INVALID_ARG;
    std::string input(json);
    const glz::error_ctx ec = allow_comments ? glz::validate_jsonc(input) : glz::validate_json(input);
    if (ec) {
        return fail(client, GAME_ERR_SCRIPT, glaze_error("json prettify", ec, input));
    }
    std::string output = allow_comments ? glz::prettify_jsonc(input) : glz::prettify_json(input);
    return copy_text(client, output, out_buf, out_cap, out_len, "json prettify output");
}

extern "C" game_error_t game_msgpack_pack_json(game_client_t* client,
                                                const char* json,
                                                uint8_t* out_buf,
                                                int out_cap,
                                                int* out_len) {
    if (!client || !client->initialized || !json) return GAME_ERR_INVALID_ARG;
    lua_State* L = get_lua_state_for_client(client);
    if (!L) return fail(client, GAME_ERR_SCRIPT, "ScriptVM not available");

    const int base = lua_gettop(L);
    lua_getglobal(L, "json");
    lua_getfield(L, -1, "decode");
    lua_remove(L, -2);
    lua_pushstring(L, json);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "json decode failed";
        lua_settop(L, base);
        return fail(client, GAME_ERR_SCRIPT, error);
    }

    lua_getglobal(L, "cmsgpack");
    lua_getfield(L, -1, "pack");
    lua_remove(L, -2);
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "msgpack pack failed";
        lua_settop(L, base);
        return fail(client, GAME_ERR_SCRIPT, error);
    }

    size_t len = 0;
    const char* bytes = lua_tolstring(L, -1, &len);
    const game_error_t rc = copy_binary(client, bytes ? bytes : "", len, out_buf, out_cap, out_len, "msgpack output");
    lua_settop(L, base);
    return rc;
}

extern "C" game_error_t game_msgpack_unpack_to_json(game_client_t* client,
                                                     const uint8_t* data,
                                                     int data_len,
                                                     char* out_buf,
                                                     int out_cap,
                                                     int* out_len) {
    if (!client || !client->initialized || !data || data_len < 0) return GAME_ERR_INVALID_ARG;
    lua_State* L = get_lua_state_for_client(client);
    if (!L) return fail(client, GAME_ERR_SCRIPT, "ScriptVM not available");

    const int base = lua_gettop(L);
    lua_getglobal(L, "cmsgpack");
    lua_getfield(L, -1, "unpack");
    lua_remove(L, -2);
    lua_pushlstring(L, reinterpret_cast<const char*>(data), static_cast<size_t>(data_len));
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "msgpack unpack failed";
        lua_settop(L, base);
        return fail(client, GAME_ERR_SCRIPT, error);
    }

    lua_getglobal(L, "json");
    lua_getfield(L, -1, "encode");
    lua_remove(L, -2);
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "json encode failed";
        lua_settop(L, base);
        return fail(client, GAME_ERR_SCRIPT, error);
    }

    size_t len = 0;
    const char* json = lua_tolstring(L, -1, &len);
    const std::string output(json ? json : "", len);
    const game_error_t rc = copy_text(client, output, out_buf, out_cap, out_len, "msgpack json output");
    lua_settop(L, base);
    return rc;
}

extern "C" game_error_t game_auth_set_token_backend(game_client_t* client) {
    if (!client) return GAME_ERR_INVALID_ARG;
    engine::auth::SessionManager::Instance().SetBackend(
        std::make_unique<engine::auth::TokenAuthBackend>());
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_auth_add_token(game_client_t* client,
                                             const char* token,
                                             const char* entity_id) {
    if (!client || !token || !entity_id) return GAME_ERR_INVALID_ARG;
    auto backend = ensure_token_backend();
    if (!backend) return fail(client, GAME_ERR_GENERIC, "failed to initialize token auth backend");
    backend->AddToken(token, entity_id);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_auth_authenticate_token(game_client_t* client,
                                                      const char* token,
                                                      char* entity_id_buf,
                                                      int entity_id_cap,
                                                      char* session_id_buf,
                                                      int session_id_cap) {
    if (!client || !token || !entity_id_buf || entity_id_cap <= 0 ||
        !session_id_buf || session_id_cap <= 0) {
        return GAME_ERR_INVALID_ARG;
    }

    auto backend = ensure_token_backend();
    if (!backend) return fail(client, GAME_ERR_GENERIC, "failed to initialize token auth backend");

    std::map<std::string, std::string> params;
    params.emplace("token", token);
    auto result = backend->Authenticate("token", params);
    if (!result.success) {
        return fail(client, GAME_ERR_NOT_FOUND,
                    result.reason.empty() ? "authentication failed" : result.reason);
    }

    if (entity_id_cap <= static_cast<int>(result.entity_id.size()) ||
        session_id_cap <= static_cast<int>(result.session_id.size())) {
        return fail(client, GAME_ERR_BUFFER_TOO_SMALL, "auth output buffer too small");
    }

    copy_fixed_string(entity_id_buf, entity_id_cap, result.entity_id);
    copy_fixed_string(session_id_buf, session_id_cap, result.session_id);
    clear_error(client);
    return GAME_OK;
}

extern "C" bool game_auth_validate_session(game_client_t* client,
                                            const char* session_id) {
    if (!client || !session_id) return false;
    auto& manager = engine::auth::SessionManager::Instance();
    auto backend = manager.GetBackendSnapshot();
    return (backend && backend->ValidateSession(session_id)) ||
           manager.IsSessionValid(session_id);
}

extern "C" game_error_t game_auth_revoke_session(game_client_t* client,
                                                  const char* session_id) {
    if (!client || !session_id) return GAME_ERR_INVALID_ARG;
    engine::auth::SessionManager::Instance().RevokeSession(session_id);
    auto backend = engine::auth::SessionManager::Instance().GetBackendSnapshot();
    if (backend) {
        backend->RevokeSession(session_id);
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_auth_grant_permission(game_client_t* client,
                                                    const char* entity_id,
                                                    const char* permission) {
    if (!client || !entity_id || !permission) return GAME_ERR_INVALID_ARG;
    auto backend = ensure_auth_backend();
    if (!backend) return fail(client, GAME_ERR_GENERIC, "failed to initialize auth backend");
    backend->GrantPermission(entity_id, permission);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_auth_revoke_permission(game_client_t* client,
                                                     const char* entity_id,
                                                     const char* permission) {
    if (!client || !entity_id || !permission) return GAME_ERR_INVALID_ARG;
    auto backend = ensure_auth_backend();
    if (!backend) return fail(client, GAME_ERR_GENERIC, "failed to initialize auth backend");
    backend->RevokePermission(entity_id, permission);
    clear_error(client);
    return GAME_OK;
}

extern "C" bool game_auth_has_permission(game_client_t* client,
                                          const char* entity_id,
                                          const char* permission) {
    if (!client || !entity_id || !permission) return false;
    auto backend = ensure_auth_backend();
    return backend && backend->HasPermission(entity_id, permission);
}

extern "C" game_error_t game_metrics_counter_inc(game_client_t* client,
                                                  const char* name,
                                                  int64_t delta) {
    if (!client || !name || !*name) return GAME_ERR_INVALID_ARG;
    engine::monitoring::MetricsRegistry::Instance().GetCounter(name).Inc(delta);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_metrics_gauge_set(game_client_t* client,
                                                const char* name,
                                                int64_t value) {
    if (!client || !name || !*name) return GAME_ERR_INVALID_ARG;
    engine::monitoring::MetricsRegistry::Instance().GetGauge(name).Set(value);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_metrics_gauge_inc(game_client_t* client,
                                                const char* name,
                                                int64_t delta) {
    if (!client || !name || !*name) return GAME_ERR_INVALID_ARG;
    engine::monitoring::MetricsRegistry::Instance().GetGauge(name).Inc(delta);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_metrics_histogram_observe(game_client_t* client,
                                                        const char* name,
                                                        double value) {
    if (!client || !name || !*name || !std::isfinite(value)) return GAME_ERR_INVALID_ARG;
    engine::monitoring::MetricsRegistry::Instance().GetHistogram(name).Observe(value);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_metrics_export_prometheus(game_client_t* client,
                                                        char* out_buf,
                                                        int out_cap,
                                                        int* out_len) {
    if (!client) return GAME_ERR_INVALID_ARG;
    return copy_text(client,
                     engine::monitoring::MetricsRegistry::Instance().ExportPrometheus(),
                     out_buf,
                     out_cap,
                     out_len,
                     "prometheus metrics");
}

extern "C" game_error_t game_metrics_export_json(game_client_t* client,
                                                  char* out_buf,
                                                  int out_cap,
                                                  int* out_len) {
    if (!client) return GAME_ERR_INVALID_ARG;
    return copy_text(client,
                     engine::monitoring::MetricsRegistry::Instance().ExportJson(),
                     out_buf,
                     out_cap,
                     out_len,
                     "json metrics");
}

extern "C" game_error_t game_entity_create(game_client_t* client,
                                            uint64_t requested_id,
                                            uint64_t* out_entity_id) {
    if (!client || !out_entity_id) return GAME_ERR_INVALID_ARG;
    auto* entity = engine::entity::EntityManager::Instance().CreateEntity(
        static_cast<engine::entity::EntityId>(requested_id));
    if (!entity) {
        return fail(client,
                    requested_id == 0 ? GAME_ERR_GENERIC : GAME_ERR_ALREADY_EXISTS,
                    "failed to create entity");
    }
    entity->Activate();
    *out_entity_id = static_cast<uint64_t>(entity->GetId());
    clear_error(client);
    return GAME_OK;
}

extern "C" bool game_entity_exists(game_client_t* client,
                                    uint64_t entity_id) {
    if (!client) return false;
    return find_entity(entity_id) != nullptr;
}

extern "C" game_error_t game_entity_destroy(game_client_t* client,
                                             uint64_t entity_id) {
    if (!client || entity_id == engine::entity::kInvalidEntityId) return GAME_ERR_INVALID_ARG;
    if (!find_entity(entity_id)) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    engine::entity::EntityManager::Instance().DestroyEntity(
        static_cast<engine::entity::EntityId>(entity_id));
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_count(game_client_t* client,
                                           uint64_t* out_count) {
    if (!client || !out_count) return GAME_ERR_INVALID_ARG;
    *out_count = static_cast<uint64_t>(engine::entity::EntityManager::Instance().Count());
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_set_attr_string(game_client_t* client,
                                                     uint64_t entity_id,
                                                     const char* key,
                                                     const char* value) {
    if (!client || !key || !value) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    entity->Attrs().Set(key, std::string(value));
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_set_attr_int(game_client_t* client,
                                                  uint64_t entity_id,
                                                  const char* key,
                                                  int64_t value) {
    if (!client || !key) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    entity->Attrs().Set(key, value);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_set_attr_double(game_client_t* client,
                                                     uint64_t entity_id,
                                                     const char* key,
                                                     double value) {
    if (!client || !key || !std::isfinite(value)) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    entity->Attrs().Set(key, value);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_set_attr_bool(game_client_t* client,
                                                   uint64_t entity_id,
                                                   const char* key,
                                                   bool value) {
    if (!client || !key) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    entity->Attrs().Set(key, value);
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_get_attr_string(game_client_t* client,
                                                     uint64_t entity_id,
                                                     const char* key,
                                                     char* out_buf,
                                                     int out_cap,
                                                     int* out_len) {
    if (!client || !key) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    const auto* value = entity->Attrs().TryGet(key);
    if (!value) return fail(client, GAME_ERR_NOT_FOUND, "entity attribute not found");
    return copy_text(client, attr_to_string(*value), out_buf, out_cap, out_len, "entity attribute");
}

extern "C" game_error_t game_entity_get_attr_int(game_client_t* client,
                                                  uint64_t entity_id,
                                                  const char* key,
                                                  int64_t* out_value) {
    if (!client || !key || !out_value) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    const auto* value = entity->Attrs().TryGet(key);
    if (!value) return fail(client, GAME_ERR_NOT_FOUND, "entity attribute not found");
    if (const auto* int_value = std::get_if<int64_t>(value)) {
        *out_value = *int_value;
        clear_error(client);
        return GAME_OK;
    }
    return fail(client, GAME_ERR_INVALID_ARG, "entity attribute is not an integer");
}

extern "C" game_error_t game_entity_get_attr_double(game_client_t* client,
                                                     uint64_t entity_id,
                                                     const char* key,
                                                     double* out_value) {
    if (!client || !key || !out_value) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    const auto* value = entity->Attrs().TryGet(key);
    if (!value) return fail(client, GAME_ERR_NOT_FOUND, "entity attribute not found");
    if (const auto* double_value = std::get_if<double>(value)) {
        *out_value = *double_value;
    } else if (const auto* int_value = std::get_if<int64_t>(value)) {
        *out_value = static_cast<double>(*int_value);
    } else {
        return fail(client, GAME_ERR_INVALID_ARG, "entity attribute is not numeric");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_get_attr_bool(game_client_t* client,
                                                   uint64_t entity_id,
                                                   const char* key,
                                                   bool* out_value) {
    if (!client || !key || !out_value) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    const auto* value = entity->Attrs().TryGet(key);
    if (!value) return fail(client, GAME_ERR_NOT_FOUND, "entity attribute not found");
    if (const auto* bool_value = std::get_if<bool>(value)) {
        *out_value = *bool_value;
        clear_error(client);
        return GAME_OK;
    }
    return fail(client, GAME_ERR_INVALID_ARG, "entity attribute is not boolean");
}

extern "C" game_error_t game_entity_remove_attr(game_client_t* client,
                                                 uint64_t entity_id,
                                                 const char* key) {
    if (!client || !key) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    if (!entity->Attrs().Remove(key)) {
        return fail(client, GAME_ERR_NOT_FOUND, "entity attribute not found");
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_entity_attr_count(game_client_t* client,
                                                uint64_t entity_id,
                                                uint64_t* out_count) {
    if (!client || !out_count) return GAME_ERR_INVALID_ARG;
    auto* entity = find_entity(entity_id);
    if (!entity) return fail(client, GAME_ERR_NOT_FOUND, "entity not found");
    *out_count = static_cast<uint64_t>(entity->Attrs().Count());
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_space_create(game_client_t* client,
                                           const char* name,
                                           uint64_t max_entities,
                                           uint64_t max_players,
                                           uint64_t* out_space_id) {
    if (!client || !name || !out_space_id) return GAME_ERR_INVALID_ARG;
    if (max_entities > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
        max_players > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
        return GAME_ERR_INVALID_ARG;
    }

    engine::space::SpaceConfig config;
    config.name = name;
    if (max_entities > 0) config.max_entities = static_cast<size_t>(max_entities);
    if (max_players > 0) config.max_players = static_cast<size_t>(max_players);

    auto* space = engine::space::SpaceManager::Instance().CreateSpace(config);
    if (!space) return fail(client, GAME_ERR_GENERIC, "failed to create space");
    *out_space_id = static_cast<uint64_t>(space->GetId());
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_space_destroy(game_client_t* client,
                                            uint64_t space_id) {
    if (!client || space_id == engine::space::kInvalidSpaceId) return GAME_ERR_INVALID_ARG;
    auto& manager = engine::space::SpaceManager::Instance();
    if (!manager.GetSpace(static_cast<engine::space::SpaceId>(space_id))) {
        return fail(client, GAME_ERR_NOT_FOUND, "space not found");
    }
    manager.DestroySpace(static_cast<engine::space::SpaceId>(space_id));
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_space_count(game_client_t* client,
                                          uint64_t* out_count) {
    if (!client || !out_count) return GAME_ERR_INVALID_ARG;
    *out_count = static_cast<uint64_t>(engine::space::SpaceManager::Instance().SpaceCount());
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_aoi_create(game_client_t* client,
                                         float world_width,
                                         float world_height,
                                         float cell_size,
                                         game_aoi_t** out_aoi) {
    if (out_aoi) {
        *out_aoi = nullptr;
    }
    if (!client || !out_aoi || !valid_positive_float(world_width) ||
        !valid_positive_float(world_height) || !valid_positive_float(cell_size)) {
        return GAME_ERR_INVALID_ARG;
    }
    auto* handle = new (std::nothrow) game_aoi_t();
    if (!handle) return GAME_ERR_OUT_OF_MEMORY;

    try {
        auto grid = std::make_unique<engine::aoi::SpatialGrid>(world_width, world_height, cell_size);
        handle->manager = std::make_unique<engine::aoi::AOIManager>(std::move(grid));
    } catch (...) {
        delete handle;
        return fail(client, GAME_ERR_GENERIC, "failed to create AOI manager");
    }

    *out_aoi = handle;
    clear_error(client);
    return GAME_OK;
}

extern "C" void game_aoi_destroy(game_aoi_t** aoi) {
    if (!aoi || !*aoi) return;
    delete *aoi;
    *aoi = nullptr;
}

extern "C" game_error_t game_aoi_register_entity(game_aoi_t* aoi,
                                                  uint64_t entity_id,
                                                  float x,
                                                  float y,
                                                  float radius) {
    if (!aoi || !aoi->manager || entity_id == engine::entity::kInvalidEntityId ||
        !valid_finite_float(x) || !valid_finite_float(y) || radius < 0.0f ||
        !valid_finite_float(radius)) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        aoi->manager->UpsertEntity(static_cast<engine::entity::EntityId>(entity_id), x, y, radius);
        return GAME_OK;
    } catch (...) {
        return aoi_exception_to_error();
    }
}

extern "C" game_error_t game_aoi_move_entity(game_aoi_t* aoi,
                                              uint64_t entity_id,
                                              float x,
                                              float y) {
    if (!aoi || !aoi->manager || entity_id == engine::entity::kInvalidEntityId ||
        !valid_finite_float(x) || !valid_finite_float(y)) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        aoi->manager->OnEntityMove(static_cast<engine::entity::EntityId>(entity_id), x, y);
        return GAME_OK;
    } catch (...) {
        return aoi_exception_to_error();
    }
}

extern "C" game_error_t game_aoi_update_radius(game_aoi_t* aoi,
                                                uint64_t entity_id,
                                                float radius) {
    if (!aoi || !aoi->manager || entity_id == engine::entity::kInvalidEntityId ||
        radius < 0.0f || !valid_finite_float(radius)) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        aoi->manager->UpdateEntityRadius(static_cast<engine::entity::EntityId>(entity_id), radius);
        return GAME_OK;
    } catch (...) {
        return aoi_exception_to_error();
    }
}

extern "C" game_error_t game_aoi_unregister_entity(game_aoi_t* aoi,
                                                    uint64_t entity_id) {
    if (!aoi || !aoi->manager || entity_id == engine::entity::kInvalidEntityId) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        aoi->manager->UnregisterEntity(static_cast<engine::entity::EntityId>(entity_id));
        return GAME_OK;
    } catch (...) {
        return aoi_exception_to_error();
    }
}

extern "C" game_error_t game_aoi_count(game_aoi_t* aoi,
                                        uint64_t* out_count) {
    if (!aoi || !aoi->manager || !out_count) return GAME_ERR_INVALID_ARG;
    *out_count = static_cast<uint64_t>(aoi->manager->EntityCount());
    return GAME_OK;
}

extern "C" game_error_t game_aoi_query_radius(game_aoi_t* aoi,
                                               float x,
                                               float y,
                                               float radius,
                                               uint64_t* out_ids,
                                               int out_cap,
                                               int* out_count) {
    if (!aoi || !aoi->manager || !valid_finite_float(x) || !valid_finite_float(y) ||
        radius < 0.0f || !valid_finite_float(radius)) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        return copy_entity_ids(aoi->manager->QueryRadius(x, y, radius), out_ids, out_cap, out_count);
    } catch (...) {
        return aoi_exception_to_error();
    }
}

extern "C" game_error_t game_aoi_get_visible(game_aoi_t* aoi,
                                              uint64_t entity_id,
                                              uint64_t* out_ids,
                                              int out_cap,
                                              int* out_count) {
    if (!aoi || !aoi->manager || entity_id == engine::entity::kInvalidEntityId) {
        return GAME_ERR_INVALID_ARG;
    }
    try {
        return copy_entity_ids(
            aoi->manager->GetVisibleEntities(static_cast<engine::entity::EntityId>(entity_id)),
            out_ids,
            out_cap,
            out_count);
    } catch (...) {
        return aoi_exception_to_error();
    }
}
