#include "engine/vm/vm.h"

#include <filesystem>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"

namespace engine {

ScriptVM::ScriptVM() {
    L_ = luaL_newstate();
    if (L_) {
        luaL_openlibs(L_);
    }
}

ScriptVM::~ScriptVM() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

ScriptVM::ScriptVM(ScriptVM&& other) noexcept : L_(other.L_) {
    other.L_ = nullptr;
    callbacks_ = std::move(other.callbacks_);
}

ScriptVM& ScriptVM::operator=(ScriptVM&& other) noexcept {
    if (this != &other) {
        if (L_) {
            lua_close(L_);
        }
        L_ = other.L_;
        other.L_ = nullptr;
        callbacks_ = std::move(other.callbacks_);
    }
    return *this;
}

//=================================================================
// Script lifecycle helpers
//=================================================================

void ScriptVM::CallGlobalFunction(std::string_view name) {
    if (!L_) return;

    lua_getglobal(L_, name.data());
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        lua_pop(L_, 1);
        return;
    }

    int rc = lua_pcall(L_, 0, 0, 0);
    if (rc != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ScriptVM::{} error: [{}]",
                         name, lua_tostring(L_, -1));
        lua_pop(L_, 1);
    }
}

void ScriptVM::InitScript() {
    CallGlobalFunction("InitScript");
}

void ScriptVM::UpdateScript() {
    CallGlobalFunction("UpdateScript");
}

void ScriptVM::DestroyScript() {
    CallGlobalFunction("DestroyScript");
}

//=================================================================
// Script execution
//=================================================================

bool ScriptVM::DoString(std::string_view script,
                  std::string_view chunk_name,
                  std::string* error_out) {
    if (!L_) {
        if (error_out) *error_out = "ScriptVM not initialized";
        return false;
    }

    int rc = luaL_loadbufferx(L_, script.data(), script.size(),
                               chunk_name.data(), "t");
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoString load error: [{}]", msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    rc = lua_pcall(L_, 0, 0, 0);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoString run error: [{}]", msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    return true;
}

bool ScriptVM::DoFile(const std::string& filename, std::string* error_out) {
    if (!L_) {
        if (error_out) *error_out = "ScriptVM not initialized";
        return false;
    }

    int rc = luaL_loadfilex(L_, filename.c_str(), nullptr);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile load error [{}]: [{}]",
                         filename, msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    rc = lua_pcall(L_, 0, 0, 0);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile run error [{}]: [{}]",
                         filename, msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    return true;
}

size_t ScriptVM::DoDirectory(const std::string& dir_path) {
    if (!L_) return 0;

    size_t failures = 0;
    auto* logger = GetLogger();

    std::error_code ec;
    auto status = std::filesystem::status(dir_path, ec);
    if (ec || !std::filesystem::is_directory(status)) {
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoDirectory path not a directory: [{}]",
                         dir_path);
        return 1;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (ec) break;

        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        if (ext != ".lua" && ext != ".LUA") continue;

        auto filepath = entry.path().string();
        ENGINE_LOG_INFO(logger, "ScriptVM::DoDirectory loading [{}]", filepath);

        if (!DoFile(filepath)) {
            ++failures;
        }
    }

    return failures;
}

//=================================================================
// C function / module registration
//=================================================================

void ScriptVM::RegisterFunction(std::string_view name, lua_CFunction func) {
    if (!L_) return;
    lua_pushcfunction(L_, func);
    lua_setglobal(L_, name.data());
}

void ScriptVM::RegisterFunctions(const luaL_Reg* functions) {
    if (!L_ || !functions) return;

    lua_pushglobaltable(L_);
    for (const luaL_Reg* r = functions; r->name != nullptr; ++r) {
        lua_pushcfunction(L_, r->func);
        lua_setfield(L_, -2, r->name);
    }
    lua_pop(L_, 1);
}

void ScriptVM::RegisterModule(std::string_view name, const luaL_Reg* functions) {
    if (!L_) return;

    luaL_newlib(L_, functions);
    lua_setglobal(L_, name.data());
}

void ScriptVM::RegisterModuleOpen(std::string_view name, lua_CFunction openf,
                            bool make_global) {
    if (!L_) return;

    luaL_requiref(L_, name.data(), openf, make_global ? 1 : 0);
    lua_pop(L_, 1);
}

//=================================================================
// Convenience getters
//=================================================================

std::string ScriptVM::ToString(int index) {
    if (!L_) return {};

    size_t len = 0;
    const char* s = lua_tolstring(L_, index, &len);
    if (s) {
        return std::string(s, len);
    }
    return {};
}

const char* ScriptVM::LuaVersion() {
    return LUA_VERSION;
}

//=================================================================
// RegisterCallback
//=================================================================

void ScriptVM::RegisterCallback(std::string_view name, LuaCallback callback) {
    if (!L_) return;

    callbacks_.push_back(std::move(callback));
    auto* ptr = &callbacks_.back();

    lua_pushlightuserdata(L_, ptr);
    lua_pushcclosure(L_, &ScriptVM::CallbackTrampoline, 1);
    lua_setglobal(L_, name.data());
}

int ScriptVM::CallbackTrampoline(lua_State* L) {
    auto* cb = static_cast<LuaCallback*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (cb && *cb) {
        return (*cb)(L);
    }
    return 0;
}

} // namespace engine
