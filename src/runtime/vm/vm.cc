#include "runtime/vm/vm.h"

#include <chrono>
#include <filesystem>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {

ScriptVM::ScriptVM() {
    ENGINE_PROFILE_SCOPE("engine.vm", "ScriptVM::ctor");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: creating lua state...");

    L_ = luaL_newstate();
    if (!L_) {
        ENGINE_LOG_CRITICAL(logger, "ScriptVM: luaL_newstate() returned nullptr");
        abort();
    }

    luaL_openlibs(L_);
    ENGINE_LOG_INFO(logger, "ScriptVM: lua state created, version=[{}], "
                    "base libs loaded (basic/coroutine/table/io/os/string/"
                    "math/utf8/debug/package)", LUA_VERSION);

    int mem_kb = lua_gc(L_, LUA_GCCOUNT, 0);
    ENGINE_LOG_INFO(logger, "ScriptVM: initial memory usage [{} KB]", mem_kb);
}

ScriptVM::~ScriptVM() {
    ENGINE_PROFILE_SCOPE("engine.vm", "ScriptVM::dtor");

    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

ScriptVM::ScriptVM(ScriptVM&& other) noexcept : L_(other.L_) {
    other.L_ = nullptr;
    callbacks_ = std::move(other.callbacks_);
    importer_ = std::move(other.importer_);
}

ScriptVM& ScriptVM::operator=(ScriptVM&& other) noexcept {
    if (this != &other) {
        if (L_) {
            lua_close(L_);
        }
        L_ = other.L_;
        other.L_ = nullptr;
        callbacks_ = std::move(other.callbacks_);
        importer_ = std::move(other.importer_);
    }
    return *this;
}

//=================================================================
// Script lifecycle helpers
//=================================================================

void ScriptVM::CallGlobalFunction(std::string_view name) {
    if (!L_) return;

    lua_getglobal(L_, std::string(name).c_str());
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        lua_pop(L_, 1);
        return;
    }

    int rc = lua_pcall(L_, 0, 0, 0);
    if (rc != LUA_OK) {
        auto* logger = GetLogger();
        if (name == "UpdateScript") {
            ENGINE_LOG_DEBUG_LIMIT(std::chrono::seconds(5), logger,
                                   "ScriptVM: [{}] error: [{}]",
                                   name, lua_tostring(L_, -1));
        } else {
            ENGINE_LOG_ERROR(logger, "ScriptVM: [{}] error: [{}]",
                             name, lua_tostring(L_, -1));
        }
        lua_pop(L_, 1);
    }
}

void ScriptVM::InitScript() {
    ENGINE_PROFILE_SCOPE("engine.vm", "InitScript");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: === InitScript phase ===");
    CallGlobalFunction("InitScript");
}

void ScriptVM::UpdateScript() {
    CallGlobalFunction("UpdateScript");
}

void ScriptVM::DestroyScript() {
    ENGINE_PROFILE_SCOPE("engine.vm", "DestroyScript");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: === DestroyScript phase ===");
    CallGlobalFunction("DestroyScript");
}

//=================================================================
// Script execution
//=================================================================

bool ScriptVM::DoString(std::string_view script,
                  std::string_view chunk_name,
                  std::string* error_out) {
    ENGINE_PROFILE_SCOPE("engine.script", "DoString",
        "chunk", std::string(chunk_name).c_str());

    if (!L_) {
        if (error_out) *error_out = "ScriptVM not initialized";
        return false;
    }

    int rc = luaL_loadbufferx(L_, script.data(), script.size(),
                               std::string(chunk_name).c_str(), "t");
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

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM::DoString [{}]: [{} bytes] OK",
                    chunk_name, script.size());
    return true;
}

bool ScriptVM::DoFile(const std::string& filename, std::string* error_out) {
    ENGINE_PROFILE_SCRIPT_DOFILE(filename.c_str());

    if (!L_) {
        if (error_out) *error_out = "ScriptVM not initialized";
        return false;
    }

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM::DoFile loading [{}]...", filename);

    int rc = luaL_loadfilex(L_, filename.c_str(), nullptr);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile load error [{}]: [{}]",
                         filename, msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    rc = lua_pcall(L_, 0, 0, 0);
    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile run error [{}]: [{}]",
                         filename, msg);
        if (error_out) *error_out = msg;
        lua_pop(L_, 1);
        return false;
    }

    ENGINE_LOG_INFO(logger, "ScriptVM::DoFile [{}] OK", filename);
    return true;
}

size_t ScriptVM::DoDirectory(const std::string& dir_path) {
    if (!L_) return 0;

    ENGINE_PROFILE_SCOPE("engine.script", "DoDirectory", "dir", dir_path.c_str());

    size_t failures = 0;
    size_t loaded = 0;
    auto* logger = GetLogger();

    ENGINE_LOG_INFO(logger, "ScriptVM::DoDirectory scanning [{}]...", dir_path);

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

        if (DoFile(filepath)) {
            ++loaded;
        } else {
            ++failures;
        }
    }

    if (ec) {
        ENGINE_LOG_ERROR(logger, "ScriptVM::DoDirectory iteration error in [{}]: [{}]",
                         dir_path, ec.message());
        ++failures;
    }

    ENGINE_LOG_INFO(logger, "ScriptVM::DoDirectory [{}] done: "
                    "[{}] loaded, [{}] failed", dir_path, loaded, failures);
    return failures;
}

//=================================================================
// C function / module registration
//=================================================================

void ScriptVM::RegisterFunction(std::string_view name, lua_CFunction func) {
    if (!L_ || !func) return;
    lua_pushcfunction(L_, func);
    lua_setglobal(L_, std::string(name).c_str());
    auto* logger = GetLogger();
    ENGINE_LOG_DEBUG(logger, "ScriptVM: registered C function [{}]", name);
}

void ScriptVM::RegisterFunctions(const luaL_Reg* functions) {
    if (!L_ || !functions) return;

    size_t count = 0;
    lua_pushglobaltable(L_);
    for (const luaL_Reg* r = functions; r->name != nullptr; ++r) {
        if (!r->func) continue;
        lua_pushcfunction(L_, r->func);
        lua_setfield(L_, -2, r->name);
        ++count;
    }
    lua_pop(L_, 1);

    auto* logger = GetLogger();
    ENGINE_LOG_DEBUG(logger, "ScriptVM: registered [{}] C functions from array",
                    count);
}

void ScriptVM::RegisterModule(std::string_view name, const luaL_Reg* functions) {
    if (!L_ || !functions) return;

    luaL_newlib(L_, functions);
    lua_setglobal(L_, std::string(name).c_str());

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: registered module [{}]", name);
}

void ScriptVM::RegisterModuleOpen(std::string_view name, lua_CFunction openf,
                            bool make_global) {
    if (!L_) return;

    luaL_requiref(L_, std::string(name).c_str(), openf, make_global ? 1 : 0);
    lua_pop(L_, 1);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: registered module [{}] (openf, global=[{}])",
                    name, make_global);
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
// Module import system
//=================================================================

ScriptImporter& ScriptVM::GetImporter() {
    if (!importer_) {
        importer_ = std::make_unique<ScriptImporter>();
    }
    return *importer_;
}

void ScriptVM::SetImportPath(const std::string& scripts_dir) {
    GetImporter().Init(scripts_dir);
}

//=================================================================
// RegisterCallback
//=================================================================

void ScriptVM::RegisterCallback(std::string_view name, LuaCallback callback) {
    if (!L_) return;

    auto cb = std::make_unique<LuaCallback>(std::move(callback));
    // The raw pointer is stable (heap-allocated object never moves) so long as
    // callbacks_ is append-only. Do NOT add any erasure from this vector.
    auto* ptr = cb.get();
    callbacks_.push_back(std::move(cb));

    lua_pushlightuserdata(L_, ptr);
    lua_pushcclosure(L_, &ScriptVM::CallbackTrampoline, 1);
    lua_setglobal(L_, std::string(name).c_str());

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptVM: registered callback [{}] (total callbacks: [{}])",
                    name, callbacks_.size());
}

int ScriptVM::CallbackTrampoline(lua_State* L) {
    auto* cb = static_cast<LuaCallback*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (cb && *cb) {
        return (*cb)(L);
    }
    return 0;
}

} // namespace engine
