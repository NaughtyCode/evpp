#include "runtime/vm/vm.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/vm/lua_error_handler.h"

namespace engine {

namespace {

std::vector<std::string> SplitPathList(const std::string& paths) {
	std::vector<std::string> result;
	size_t start = 0;
	while (start <= paths.size()) {
		size_t end = paths.find(';', start);
		if (end == std::string::npos) end = paths.size();
		std::string path = paths.substr(start, end - start);
		if (!path.empty()) {
#ifdef _WIN32
			std::replace(path.begin(), path.end(), '\\', '/');
#endif
			result.push_back(std::move(path));
		}
		if (end == paths.size()) break;
		start = end + 1;
	}
	return result;
}

std::filesystem::path AbsoluteNormalPath(const std::filesystem::path& path) {
	std::error_code ec;
	auto absolute = std::filesystem::absolute(path, ec);
	if (ec) {
		return path.lexically_normal();
	}
	return absolute.lexically_normal();
}

bool IsUsableRelativePath(const std::filesystem::path& relative) {
	if (relative.empty() || relative.is_absolute()) return false;
	const auto rel_str = relative.generic_string();
	if (rel_str.empty() || rel_str == ".") return false;
	for (const auto& part : relative) {
		if (part == "..") return false;
	}
	return true;
}

bool IsPathUnderOrAtRoot(const std::filesystem::path& path,
						 const std::filesystem::path& root) {
	auto relative = path.lexically_relative(root);
	return relative.generic_string() == "." || IsUsableRelativePath(relative);
}

std::filesystem::path CommonRootForPaths(const std::vector<std::string>& paths) {
	if (paths.empty()) return {};

	std::filesystem::path common = AbsoluteNormalPath(paths.front());
	for (size_t i = 1; i < paths.size(); ++i) {
		const auto path = AbsoluteNormalPath(paths[i]);
		while (!common.empty() && !IsPathUnderOrAtRoot(path, common)) {
			auto parent = common.parent_path();
			if (parent == common) break;
			common = parent;
		}
	}
	return common;
}

std::vector<std::string> DeriveScriptRootsFromImportPath(const std::string& paths) {
	auto split_paths = SplitPathList(paths);
	if (split_paths.size() <= 1) return split_paths;

	auto common = CommonRootForPaths(split_paths);
	if (!common.empty()) {
		return {common.string()};
	}
	return split_paths;
}

std::filesystem::path RemoveLuaExtension(std::filesystem::path path) {
	auto ext = path.extension().string();
	if (ext == ".lua" || ext == ".LUA") {
		path = path.parent_path() / path.stem();
	}
	return path;
}

}  // namespace

ScriptVM::ScriptVM(LuaSandboxLevel level) {
	ENGINE_PROFILE_SCOPE("engine.vm", "ScriptVM::ctor");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptVM: creating lua state...");

	L_ = luaL_newstate();
	if (!L_) {
		ENGINE_LOG_CRITICAL(logger, "ScriptVM: luaL_newstate() returned nullptr");
		throw std::runtime_error("ScriptVM: luaL_newstate() failed — out of memory");
	}

	luaL_openlibs_sandboxed(L_, level);
	ENGINE_LOG_INFO(logger,
					"ScriptVM: lua state created, version=[{}]",
					LUA_VERSION);

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
	script_roots_ = std::move(other.script_roots_);
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
		script_roots_ = std::move(other.script_roots_);
	}
	return *this;
}

// Script lifecycle helpers

void ScriptVM::CallGlobalFunction(std::string_view name) {
	if (!L_) return;

	int base_top = lua_gettop(L_);
	lua_getglobal(L_, std::string(name).c_str());
	if (lua_type(L_, -1) != LUA_TFUNCTION) {
		lua_pop(L_, 1);
		return;
	}

	int f_idx = lua_gettop(L_);
	int err_idx = PushLuaErrorHandler(L_);
	(void) err_idx;
	lua_insert(L_, f_idx);
	int rc = lua_pcall(L_, 0, 0, f_idx);
	if (rc != LUA_OK) {
		auto* logger = GetLogger();
		if (name == "UpdateScript") {
			ENGINE_LOG_DEBUG_LIMIT(std::chrono::seconds(5),
								   logger,
								   "ScriptVM: [{}] error: [{}]",
								   name,
								   lua_tostring(L_, -1));
		} else {
			ENGINE_LOG_ERROR(logger, "ScriptVM: [{}] error: [{}]", name, lua_tostring(L_, -1));
		}
		lua_settop(L_, base_top);
		return;
	}
	lua_remove(L_, f_idx);
	lua_settop(L_, base_top);
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

// Script execution

// Maximum script size for DoString to prevent memory exhaustion (1 MB).
static constexpr size_t kMaxDoStringSize = 1024 * 1024;

bool ScriptVM::DoString(std::string_view script,
						std::string_view chunk_name,
						std::string* error_out,
						std::string* result_out) {
	ENGINE_PROFILE_SCOPE("engine.script", "DoString", "chunk", std::string(chunk_name).c_str());
	if (error_out) error_out->clear();
	if (result_out) result_out->clear();

	if (!L_) {
		if (error_out) *error_out = "ScriptVM not initialized";
		return false;
	}

	int base_top = lua_gettop(L_);
	if (script.size() > kMaxDoStringSize) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoString rejected: script size [{}] exceeds limit [{}]",
						 script.size(), kMaxDoStringSize);
		if (error_out) *error_out = "script exceeds maximum size";
		lua_settop(L_, base_top);
		return false;
	}

	int rc =
		luaL_loadbufferx(L_, script.data(), script.size(), std::string(chunk_name).c_str(), "t");
	if (rc != LUA_OK) {
		const char* msg = lua_tostring(L_, -1);
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoString load error: [{}]", msg);
		if (error_out) *error_out = msg ? msg : "unknown Lua load error";
		lua_settop(L_, base_top);
		return false;
	}

	int nresults = result_out ? 1 : 0;
	int msgh = PushLuaErrorHandlerForCall(L_, 0);
	rc = lua_pcall(L_, 0, nresults, msgh);
	if (rc != LUA_OK) {
		const char* msg = lua_tostring(L_, -1);
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoString run error: [{}]", msg);
		if (error_out) *error_out = msg ? msg : "unknown Lua runtime error";
		lua_settop(L_, base_top);
		return false;
	}
	lua_remove(L_, msgh);

	if (result_out) {
		if (lua_gettop(L_) > 0) {
			size_t len;
			const char* s = luaL_tolstring(L_, -1, &len);
			if (s) {
				result_out->assign(s, len);
				lua_pop(L_, 1);
			}
		}
	}
	lua_settop(L_, base_top);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptVM::DoString [{}]: [{} bytes] OK", chunk_name, script.size());
	return true;
}

bool ScriptVM::DoFile(const std::string& filename, std::string* error_out) {
	ENGINE_PROFILE_SCRIPT_DOFILE(filename.c_str());
	if (error_out) error_out->clear();

	if (!L_) {
		if (error_out) *error_out = "ScriptVM not initialized";
		return false;
	}

	int base_top = lua_gettop(L_);
	auto* logger = GetLogger();
	const std::string module_name = GetDefaultModuleNameForFile(filename);
	ENGINE_LOG_INFO(logger,
					"ScriptVM::DoFile loading [{}] as module [{}]...",
					filename,
					module_name);

	int rc = luaL_loadfilex(L_, filename.c_str(), nullptr);
	if (rc != LUA_OK) {
		const char* msg = lua_tostring(L_, -1);
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile load error [{}]: [{}]", filename, msg);
		if (error_out) *error_out = msg ? msg : "unknown Lua load error";
		lua_settop(L_, base_top);
		return false;
	}

	int msgh = PushLuaErrorHandlerForCall(L_, 0);
	rc = lua_pcall(L_, 0, 1, msgh);
	if (rc != LUA_OK) {
		const char* msg = lua_tostring(L_, -1);
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoFile run error [{}]: [{}]", filename, msg);
		if (error_out) *error_out = msg ? msg : "unknown Lua runtime error";
		lua_settop(L_, base_top);
		return false;
	}
	lua_remove(L_, msgh);
	RegisterLoadedModule(module_name, -1);
	lua_settop(L_, base_top);

	ENGINE_LOG_INFO(logger, "ScriptVM::DoFile [{}] module [{}] OK", filename, module_name);
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
		ENGINE_LOG_ERROR(logger, "ScriptVM::DoDirectory path not a directory: [{}]", dir_path);
		return 1;
	}

	std::vector<std::string> files;
	for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
		if (ec) break;

		if (!entry.is_regular_file()) continue;

		auto ext = entry.path().extension().string();
		if (ext != ".lua" && ext != ".LUA") continue;

		files.push_back(entry.path().string());
	}
	std::sort(files.begin(), files.end());

	for (const auto& filepath : files) {
		ENGINE_LOG_INFO(logger, "ScriptVM::DoDirectory loading [{}]", filepath);

		if (DoFile(filepath)) {
			++loaded;
		} else {
			++failures;
		}
	}

	if (ec) {
		ENGINE_LOG_ERROR(
			logger, "ScriptVM::DoDirectory iteration error in [{}]: [{}]", dir_path, ec.message());
		++failures;
	}

	ENGINE_LOG_INFO(logger,
					"ScriptVM::DoDirectory [{}] done: "
					"[{}] loaded, [{}] failed",
					dir_path,
					loaded,
					failures);
	return failures;
}

// C function / module registration

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
	ENGINE_LOG_DEBUG(logger, "ScriptVM: registered [{}] C functions from array", count);
}

void ScriptVM::RegisterModule(std::string_view name, const luaL_Reg* functions) {
	if (!L_ || !functions) return;

	luaL_newlib(L_, functions);
	lua_setglobal(L_, std::string(name).c_str());

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptVM: registered module [{}]", name);
}

void ScriptVM::RegisterModuleOpen(std::string_view name, lua_CFunction openf, bool make_global) {
	if (!L_ || !openf) return;

	luaL_requiref(L_, std::string(name).c_str(), openf, make_global ? 1 : 0);
	lua_pop(L_, 1);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(
		logger, "ScriptVM: registered module [{}] (openf, global=[{}])", name, make_global);
}

// Custom pointer store — per-VM void* array (backed by global_State)

bool ScriptVM::ReserveCustomPtrSlots(int total_slots) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Reserve(total_slots);
}

void ScriptVM::SetCustomPtr(int index, void* ptr) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	store.Set(index, ptr);
}

void* ScriptVM::GetCustomPtr(int index) const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Get(index);
}

int ScriptVM::PushCustomPtr(void* ptr) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Push(ptr);
}

void ScriptVM::SetNullCustomPtr(int index) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	store.SetNull(index);
}

void ScriptVM::ClearCustomPtrs() {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	store.Clear();
}

int ScriptVM::CustomPtrCount() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Count();
}

int ScriptVM::CustomPtrCapacity() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Capacity();
}

bool ScriptVM::HasCustomPtr(int index) const {
	return GetCustomPtr(index) != nullptr;
}

int ScriptVM::FindCustomPtr(void* ptr) const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Find(ptr);
}

bool ScriptVM::ContainsCustomPtr(void* ptr) const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.Contains(ptr);
}

int ScriptVM::CopyCustomPtrsTo(void** dst, int max_count) const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.CopyTo(dst, max_count);
}

void ScriptVM::CopyCustomPtrsFrom(void* const* src, int count) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	store.CopyFrom(src, count);
}

// Convenience getters

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

// Module import system

ScriptImporter& ScriptVM::GetImporter() {
	if (!importer_) {
		importer_ = std::make_unique<ScriptImporter>();
	}
	return *importer_;
}

void ScriptVM::SetImportPath(const std::string& scripts_dir) {
	if (scripts_dir.find(';') != std::string::npos) {
		GetImporter().SetPaths(scripts_dir);
	} else {
		GetImporter().Init(scripts_dir);
	}
	SetScriptRoots(DeriveScriptRootsFromImportPath(scripts_dir));
}

void ScriptVM::SetScriptRoot(const std::string& root_dir) {
	SetScriptRoots({root_dir});
}

void ScriptVM::SetScriptRoots(std::vector<std::string> root_dirs) {
	script_roots_.clear();
	script_roots_.reserve(root_dirs.size());
	for (auto& root : root_dirs) {
		if (root.empty()) continue;
#ifdef _WIN32
		std::replace(root.begin(), root.end(), '\\', '/');
#endif
		script_roots_.push_back(std::move(root));
	}
}

const std::vector<std::string>& ScriptVM::GetScriptRoots() const {
	return script_roots_;
}

std::string ScriptVM::GetDefaultModuleNameForFile(const std::string& filename) const {
	return BuildDefaultModuleNameForFile(filename, script_roots_);
}

std::string ScriptVM::BuildDefaultModuleNameForFile(const std::string& filename) {
	return BuildDefaultModuleNameForFile(filename, {});
}

std::string ScriptVM::BuildDefaultModuleNameForFile(
	const std::string& filename,
	const std::vector<std::string>& root_dirs) {
	return BuildModuleNameForFile(filename, root_dirs, '_');
}

std::string ScriptVM::BuildModuleNameForFile(const std::string& filename,
											 const std::vector<std::string>& root_dirs,
											 char path_separator) {
	std::filesystem::path file_path = AbsoluteNormalPath(filename);
	std::filesystem::path best_relative;
	size_t best_root_length = 0;

	for (const auto& root_dir : root_dirs) {
		if (root_dir.empty()) continue;
		std::filesystem::path root_path = AbsoluteNormalPath(root_dir);
		auto relative = file_path.lexically_relative(root_path);
		if (!IsUsableRelativePath(relative)) continue;

		const auto root_length = root_path.generic_string().size();
		if (best_relative.empty() || root_length > best_root_length) {
			best_relative = relative;
			best_root_length = root_length;
		}
	}

	if (best_relative.empty()) {
		std::error_code ec;
		auto cwd = std::filesystem::current_path(ec);
		if (!ec) {
			auto relative = file_path.lexically_relative(AbsoluteNormalPath(cwd));
			if (IsUsableRelativePath(relative)) {
				best_relative = relative;
			}
		}
	}

	if (best_relative.empty()) {
		std::filesystem::path raw_path(filename);
		best_relative = raw_path.is_absolute() ? raw_path.filename() : raw_path.lexically_normal();
	}

	best_relative = RemoveLuaExtension(best_relative);
	std::string module_name = best_relative.generic_string();
	for (auto& c : module_name) {
		if (c == '/' || c == '\\') c = path_separator;
	}
	while (!module_name.empty() && module_name.front() == path_separator) {
		module_name.erase(module_name.begin());
	}
	if (module_name.empty()) {
		module_name = RemoveLuaExtension(std::filesystem::path(filename).filename()).string();
	}
	return module_name;
}

void ScriptVM::RegisterLoadedModule(std::string_view module_name, int value_index) {
	if (!L_ || module_name.empty()) return;

	const int abs_value_index = lua_absindex(L_, value_index);
	lua_getglobal(L_, "package");
	if (!lua_istable(L_, -1)) {
		lua_pop(L_, 1);
		return;
	}
	lua_getfield(L_, -1, "loaded");
	if (!lua_istable(L_, -1)) {
		lua_pop(L_, 2);
		return;
	}

	if (lua_isnil(L_, abs_value_index)) {
		lua_pushboolean(L_, 1);
	} else {
		lua_pushvalue(L_, abs_value_index);
	}
	lua_setfield(L_, -2, std::string(module_name).c_str());

	lua_pop(L_, 2);
}

// RegisterCallback

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
	ENGINE_LOG_INFO(logger,
					"ScriptVM: registered callback [{}] (total callbacks: [{}])",
					name,
					callbacks_.size());
}

int ScriptVM::CallbackTrampoline(lua_State* L) {
	auto* cb = static_cast<LuaCallback*>(lua_touserdata(L, lua_upvalueindex(1)));
	if (cb && *cb) {
		try {
			return (*cb)(L);
		} catch (const std::exception& e) {
			return luaL_error(L, "C++ Lua callback exception: %s", e.what());
		} catch (...) {
			return luaL_error(L, "C++ Lua callback exception: unknown");
		}
	}
	return 0;
}

}  // namespace engine
