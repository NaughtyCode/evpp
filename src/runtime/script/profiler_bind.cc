#include "runtime/script/profiler_bind.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/core/mem/mem.h"
#include "runtime/profiler/profiler_core.h"
#include "runtime/profiler/profiler_switches.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

constexpr const char* kProfilerBindStateKey = "__ProfilerBindState";

constexpr std::array<ProfilerEventGroup, 15> kProfilerBindableGroups{{
	ProfilerEventGroup::Engine,
	ProfilerEventGroup::Frame,
	ProfilerEventGroup::Timer,
	ProfilerEventGroup::Physics,
	ProfilerEventGroup::Script,
	ProfilerEventGroup::Entity,
	ProfilerEventGroup::Space,
	ProfilerEventGroup::Aoi,
	ProfilerEventGroup::Auth,
	ProfilerEventGroup::Vm,
	ProfilerEventGroup::Network,
	ProfilerEventGroup::Rpc,
	ProfilerEventGroup::Database,
	ProfilerEventGroup::Monitoring,
	ProfilerEventGroup::Config,
}};

struct ProfilerBindState {
	lua_State* main_state = nullptr;
	std::thread::id owner_thread_id{};
	bool shutting_down = false;
};

ProfilerBindState* GetProfilerState(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, kProfilerBindStateKey);
	auto* state = static_cast<ProfilerBindState*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return state;
}

std::string NormalizeToken(std::string_view value) {
	auto begin = value.find_first_not_of(" \t\r\n");
	if (begin == std::string_view::npos) return {};
	auto end = value.find_last_not_of(" \t\r\n");
	std::string token(value.substr(begin, end - begin + 1));
	std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return token;
}

bool IsNoneGroupToken(std::string_view value) {
	auto token = NormalizeToken(value);
	return token.empty() || token == "none" || token == "off" || token == "disabled";
}

bool CheckMainThread(lua_State* L) {
	auto* state = GetProfilerState(L);
	if (!state || state->shutting_down) {
		luaL_error(L, "profiler: module is only available in the main-thread Lua VM");
		return false;
	}
	if (state->owner_thread_id != std::this_thread::get_id()) {
		luaL_error(L, "profiler: API must be called from main thread");
		return false;
	}
	return true;
}

ProfilerEventGroupMask ClampGroupMask(ProfilerEventGroupMask mask) {
	return mask & kProfilerAllEventGroups;
}

ProfilerEventGroupMask CheckGroupMaskInteger(lua_State* L, int index) {
	lua_Integer value = luaL_checkinteger(L, index);
	if (value < 0) {
		luaL_error(L, "profiler: group mask must be non-negative");
		return 0;
	}

	auto mask = static_cast<ProfilerEventGroupMask>(value);
	if ((mask & ~kProfilerAllEventGroups) != 0) {
		luaL_error(L, "profiler: group mask contains unsupported bits");
		return 0;
	}
	return mask;
}

ProfilerEventGroupMask CheckGroupMaskString(lua_State* L, int index) {
	size_t len = 0;
	const char* value = lua_tolstring(L, index, &len);
	std::string_view names(value, len);

	ProfilerEventGroupMask mask = 0;
	size_t offset = 0;
	while (offset <= names.size()) {
		auto next = names.find_first_of(",;|", offset);
		auto token = next == std::string_view::npos
						 ? names.substr(offset)
						 : names.substr(offset, next - offset);
		auto group = ProfilerEventGroupFromName(token);
		if (group == ProfilerEventGroup::All) return kProfilerAllEventGroups;
		if (group == ProfilerEventGroup::None && !IsNoneGroupToken(token)) {
			auto normalized = NormalizeToken(token);
			luaL_error(L, "profiler: unknown event group '%s'", normalized.c_str());
			return 0;
		}
		mask |= ProfilerEventGroupBit(group);
		if (next == std::string_view::npos) break;
		offset = next + 1;
	}
	return ClampGroupMask(mask);
}

ProfilerEventGroupMask CheckGroupMaskArg(lua_State* L, int index);

ProfilerEventGroup CheckGroupArg(lua_State* L, int index) {
	if (lua_isnumber(L, index)) {
		auto mask = CheckGroupMaskInteger(L, index);
		if (mask == kProfilerAllEventGroups) return ProfilerEventGroup::All;
		for (auto group : kProfilerBindableGroups) {
			if (mask == ProfilerEventGroupBit(group)) return group;
		}
		if (mask == 0) return ProfilerEventGroup::None;
		luaL_error(L, "profiler: expected a single event group");
		return ProfilerEventGroup::None;
	}

	if (lua_isstring(L, index)) {
		size_t len = 0;
		const char* value = lua_tolstring(L, index, &len);
		auto group = ProfilerEventGroupFromName(std::string_view(value, len));
		if (group == ProfilerEventGroup::None && !IsNoneGroupToken(std::string_view(value, len))) {
			luaL_error(L, "profiler: unknown event group '%s'", value ? value : "");
			return ProfilerEventGroup::None;
		}
		return group;
	}

	luaL_error(L, "profiler: expected event group string or integer, got %s",
			   luaL_typename(L, index));
	return ProfilerEventGroup::None;
}

ProfilerEventGroupMask CheckGroupMaskTable(lua_State* L, int index) {
	ProfilerEventGroupMask mask = 0;
	const int abs_index = lua_absindex(L, index);
	lua_pushnil(L);
	while (lua_next(L, abs_index) != 0) {
		mask |= CheckGroupMaskArg(L, -1);
		lua_pop(L, 1);
	}
	return ClampGroupMask(mask);
}

ProfilerEventGroupMask CheckGroupMaskArg(lua_State* L, int index) {
	if (lua_isnumber(L, index)) {
		return CheckGroupMaskInteger(L, index);
	}
	if (lua_isstring(L, index)) {
		return CheckGroupMaskString(L, index);
	}
	if (lua_istable(L, index)) {
		return CheckGroupMaskTable(L, index);
	}

	luaL_error(L, "profiler: expected group mask string, integer, or table, got %s",
			   luaL_typename(L, index));
	return 0;
}

void PushGroupMask(lua_State* L, ProfilerEventGroupMask mask) {
	mask = ClampGroupMask(mask);
	lua_pushinteger(L, static_cast<lua_Integer>(mask));
	lua_pushstring(L, FormatProfilerEventGroupMask(mask).c_str());
}

void PushString(lua_State* L, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
}

bool OptionalBoolField(lua_State* L, int table_index, const char* name, bool fallback) {
	lua_getfield(L, table_index, name);
	bool result = fallback;
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TBOOLEAN);
		result = lua_toboolean(L, -1) != 0;
	}
	lua_pop(L, 1);
	return result;
}

uint32_t OptionalU32Field(lua_State* L, int table_index, const char* name, uint32_t fallback) {
	lua_getfield(L, table_index, name);
	uint32_t result = fallback;
	if (!lua_isnil(L, -1)) {
		lua_Integer value = luaL_checkinteger(L, -1);
		if (value < 0 ||
			value > static_cast<lua_Integer>(std::numeric_limits<uint32_t>::max())) {
			luaL_error(L, "profiler: config.%s must be in uint32 range", name);
			return fallback;
		}
		result = static_cast<uint32_t>(value);
	}
	lua_pop(L, 1);
	return result;
}

std::string OptionalStringField(lua_State* L,
								int table_index,
								const char* name,
								const std::string& fallback) {
	lua_getfield(L, table_index, name);
	std::string result = fallback;
	if (!lua_isnil(L, -1)) {
		size_t len = 0;
		const char* value = luaL_checklstring(L, -1, &len);
		result.assign(value, len);
	}
	lua_pop(L, 1);
	return result;
}

ProfilerEventGroupMask OptionalGroupMaskField(lua_State* L,
											  int table_index,
											  const char* name,
											  ProfilerEventGroupMask fallback) {
	lua_getfield(L, table_index, name);
	auto result = fallback;
	if (!lua_isnil(L, -1)) {
		result = CheckGroupMaskArg(L, -1);
	}
	lua_pop(L, 1);
	return result;
}

ProfilerConfig CheckProfilerConfig(lua_State* L, int index) {
	ProfilerConfig cfg;
	if (lua_isnoneornil(L, index)) return cfg;

	luaL_checktype(L, index, LUA_TTABLE);
	const int table_index = lua_absindex(L, index);
	cfg.output_path = OptionalStringField(L, table_index, "output_path", cfg.output_path);
	cfg.buffer_size_kb = OptionalU32Field(L, table_index, "buffer_size_kb", cfg.buffer_size_kb);
	cfg.duration_ms = OptionalU32Field(L, table_index, "duration_ms", cfg.duration_ms);
	cfg.flush_interval_ms =
		OptionalU32Field(L, table_index, "flush_interval_ms", cfg.flush_interval_ms);
	cfg.write_into_file = OptionalBoolField(L, table_index, "write_into_file", cfg.write_into_file);
	cfg.runtime_enabled = OptionalBoolField(L, table_index, "runtime_enabled", cfg.runtime_enabled);
	cfg.enabled_event_groups =
		OptionalGroupMaskField(L, table_index, "enabled_event_groups", cfg.enabled_event_groups);
	return cfg;
}

void SetIntegerField(lua_State* L, const char* name, ProfilerEventGroupMask value) {
	lua_pushinteger(L, static_cast<lua_Integer>(value));
	lua_setfield(L, -2, name);
}

void SetBooleanField(lua_State* L, const char* name, bool value) {
	lua_pushboolean(L, value ? 1 : 0);
	lua_setfield(L, -2, name);
}

void SetStringField(lua_State* L, const char* name, const std::string& value) {
	PushString(L, value);
	lua_setfield(L, -2, name);
}

bool CheckBoolArg(lua_State* L, int index, const char* name) {
	if (!lua_isboolean(L, index)) {
		luaL_error(L, "profiler: %s must be boolean", name);
		return false;
	}
	return lua_toboolean(L, index) != 0;
}

int l_profiler_initialize(lua_State* L) {
	CheckMainThread(L);
	auto cfg = CheckProfilerConfig(L, 1);
	lua_pushboolean(L, ProfilerManager::Get().Initialize(cfg) ? 1 : 0);
	return 1;
}

int l_profiler_shutdown(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().Shutdown();
	return 0;
}

int l_profiler_start_session(lua_State* L) {
	CheckMainThread(L);
	lua_pushboolean(L, ProfilerManager::Get().StartSession() ? 1 : 0);
	return 1;
}

int l_profiler_stop_session(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().StopSession();
	return 0;
}

int l_profiler_is_enabled(lua_State* L) {
	CheckMainThread(L);
	lua_pushboolean(L, ProfilerManager::IsEnabled() ? 1 : 0);
	return 1;
}

int l_profiler_is_initialized(lua_State* L) {
	CheckMainThread(L);
	lua_pushboolean(L, ProfilerManager::Get().IsInitialized() ? 1 : 0);
	return 1;
}

int l_profiler_is_active(lua_State* L) {
	CheckMainThread(L);
	lua_pushboolean(L, ProfilerManager::Get().IsActive() ? 1 : 0);
	return 1;
}

int l_profiler_set_runtime_enabled(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().SetRuntimeEnabled(CheckBoolArg(L, 1, "enabled"));
	return 0;
}

int l_profiler_is_runtime_enabled(lua_State* L) {
	CheckMainThread(L);
	lua_pushboolean(L, ProfilerManager::Get().IsRuntimeEnabled() ? 1 : 0);
	return 1;
}

int l_profiler_enabled_groups(lua_State* L) {
	CheckMainThread(L);
	PushGroupMask(L, ProfilerManager::Get().EnabledEventGroups());
	return 2;
}

int l_profiler_set_enabled_groups(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().SetEnabledEventGroups(CheckGroupMaskArg(L, 1));
	PushGroupMask(L, ProfilerManager::Get().EnabledEventGroups());
	return 2;
}

int l_profiler_enable_groups(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().EnableEventGroups(CheckGroupMaskArg(L, 1));
	PushGroupMask(L, ProfilerManager::Get().EnabledEventGroups());
	return 2;
}

int l_profiler_disable_groups(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().DisableEventGroups(CheckGroupMaskArg(L, 1));
	PushGroupMask(L, ProfilerManager::Get().EnabledEventGroups());
	return 2;
}

int l_profiler_set_group_enabled(lua_State* L) {
	CheckMainThread(L);
	auto group = CheckGroupArg(L, 1);
	ProfilerManager::Get().SetEventGroupEnabled(group, CheckBoolArg(L, 2, "enabled"));
	PushGroupMask(L, ProfilerManager::Get().EnabledEventGroups());
	return 2;
}

int l_profiler_is_group_enabled(lua_State* L) {
	CheckMainThread(L);
	auto group = CheckGroupArg(L, 1);
	bool enabled = false;
	if (group == ProfilerEventGroup::All) {
		enabled = ProfilerManager::Get().IsRuntimeEnabled() &&
				  ProfilerManager::Get().EnabledEventGroups() == kProfilerAllEventGroups;
	} else {
		enabled = ProfilerManager::Get().IsEventGroupEnabled(group);
	}
	lua_pushboolean(L, enabled ? 1 : 0);
	return 1;
}

int l_profiler_parse_groups(lua_State* L) {
	CheckMainThread(L);
	PushGroupMask(L, CheckGroupMaskArg(L, 1));
	return 2;
}

int l_profiler_format_groups(lua_State* L) {
	CheckMainThread(L);
	lua_pushstring(L, FormatProfilerEventGroupMask(CheckGroupMaskArg(L, 1)).c_str());
	return 1;
}

int l_profiler_group_from_name(lua_State* L) {
	CheckMainThread(L);
	auto group = CheckGroupArg(L, 1);
	lua_pushinteger(L, static_cast<lua_Integer>(ProfilerEventGroupBit(group)));
	lua_pushstring(L, ProfilerEventGroupName(group));
	return 2;
}

int l_profiler_group_from_category(lua_State* L) {
	CheckMainThread(L);
	size_t len = 0;
	const char* category = luaL_checklstring(L, 1, &len);
	auto group = ProfilerEventGroupFromCategory(std::string_view(category, len));
	lua_pushinteger(L, static_cast<lua_Integer>(ProfilerEventGroupBit(group)));
	lua_pushstring(L, ProfilerEventGroupName(group));
	return 2;
}

int l_profiler_group_name(lua_State* L) {
	CheckMainThread(L);
	lua_pushstring(L, ProfilerEventGroupName(CheckGroupArg(L, 1)));
	return 1;
}

int l_profiler_group_category(lua_State* L) {
	CheckMainThread(L);
	lua_pushstring(L, ProfilerEventGroupCategory(CheckGroupArg(L, 1)));
	return 1;
}

int l_profiler_list_groups(lua_State* L) {
	CheckMainThread(L);
	lua_newtable(L);
	int array_index = 1;
	for (auto group : kProfilerBindableGroups) {
		lua_newtable(L);
		SetStringField(L, "name", ProfilerEventGroupName(group));
		SetStringField(L, "category", ProfilerEventGroupCategory(group));
		SetIntegerField(L, "mask", ProfilerEventGroupBit(group));
		SetBooleanField(L, "enabled", ProfilerManager::Get().IsEventGroupEnabled(group));
		lua_rawseti(L, -2, array_index++);
	}
	return 1;
}

int l_profiler_flush(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().Flush();
	return 0;
}

int l_profiler_read_trace(lua_State* L) {
	CheckMainThread(L);
	auto data = ProfilerManager::Get().ReadTrace();
	if (data.empty()) {
		lua_pushliteral(L, "");
	} else {
		lua_pushlstring(L, data.data(), data.size());
	}
	return 1;
}

int l_profiler_save_trace(lua_State* L) {
	CheckMainThread(L);
	auto path = ProfilerManager::Get().SaveTrace();
	if (path.empty()) {
		lua_pushnil(L);
	} else {
		PushString(L, path);
	}
	return 1;
}

int l_profiler_save_trace_exact(lua_State* L) {
	CheckMainThread(L);
	size_t len = 0;
	const char* path = luaL_checklstring(L, 1, &len);
	lua_pushboolean(L, ProfilerManager::Get().SaveTraceExact(std::string(path, len)) ? 1 : 0);
	return 1;
}

int l_profiler_last_saved_path(lua_State* L) {
	CheckMainThread(L);
	PushString(L, ProfilerManager::Get().LastSavedPath());
	return 1;
}

int l_profiler_cached_trace_size(lua_State* L) {
	CheckMainThread(L);
	lua_pushinteger(L, static_cast<lua_Integer>(ProfilerManager::Get().CachedTraceSize()));
	return 1;
}

int l_profiler_clear_cached_trace(lua_State* L) {
	CheckMainThread(L);
	ProfilerManager::Get().ClearCachedTrace();
	return 0;
}

int l_profiler_status(lua_State* L) {
	CheckMainThread(L);
	auto& profiler = ProfilerManager::Get();
	lua_newtable(L);
	SetBooleanField(L, "enabled", ProfilerManager::IsEnabled());
	SetBooleanField(L, "initialized", profiler.IsInitialized());
	SetBooleanField(L, "active", profiler.IsActive());
	SetBooleanField(L, "runtime_enabled", profiler.IsRuntimeEnabled());
	SetIntegerField(L, "enabled_event_groups", profiler.EnabledEventGroups());
	SetStringField(L, "enabled_event_group_names",
				   FormatProfilerEventGroupMask(profiler.EnabledEventGroups()));
	SetIntegerField(L, "cached_trace_size", profiler.CachedTraceSize());
	SetStringField(L, "last_saved_path", profiler.LastSavedPath());
	return 1;
}

const luaL_Reg kProfilerFunctions[] = {
	{"initialize", l_profiler_initialize},
	{"shutdown", l_profiler_shutdown},
	{"start_session", l_profiler_start_session},
	{"stop_session", l_profiler_stop_session},
	{"is_enabled", l_profiler_is_enabled},
	{"is_initialized", l_profiler_is_initialized},
	{"is_active", l_profiler_is_active},
	{"set_runtime_enabled", l_profiler_set_runtime_enabled},
	{"is_runtime_enabled", l_profiler_is_runtime_enabled},
	{"enabled_groups", l_profiler_enabled_groups},
	{"set_enabled_groups", l_profiler_set_enabled_groups},
	{"enable_groups", l_profiler_enable_groups},
	{"disable_groups", l_profiler_disable_groups},
	{"set_group_enabled", l_profiler_set_group_enabled},
	{"is_group_enabled", l_profiler_is_group_enabled},
	{"parse_groups", l_profiler_parse_groups},
	{"format_groups", l_profiler_format_groups},
	{"group_from_name", l_profiler_group_from_name},
	{"group_from_category", l_profiler_group_from_category},
	{"group_name", l_profiler_group_name},
	{"group_category", l_profiler_group_category},
	{"list_groups", l_profiler_list_groups},
	{"flush", l_profiler_flush},
	{"read_trace", l_profiler_read_trace},
	{"save_trace", l_profiler_save_trace},
	{"save_trace_exact", l_profiler_save_trace_exact},
	{"last_saved_path", l_profiler_last_saved_path},
	{"cached_trace_size", l_profiler_cached_trace_size},
	{"clear_cached_trace", l_profiler_clear_cached_trace},
	{"status", l_profiler_status},
	{nullptr, nullptr},
};

void SetProfilerConstants(lua_State* L) {
	lua_getglobal(L, "profiler");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	SetIntegerField(L, "GROUP_NONE", 0);
	SetIntegerField(L, "GROUP_ALL", kProfilerAllEventGroups);
	for (auto group : kProfilerBindableGroups) {
		std::string name = "GROUP_";
		auto token = NormalizeToken(ProfilerEventGroupName(group));
		for (char& ch : token) {
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}
		name += token;
		SetIntegerField(L, name.c_str(), ProfilerEventGroupBit(group));
	}
	lua_pop(L, 1);
}

}  // namespace

bool ExportProfiler(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return false;

	if (GetProfilerState(L)) {
		ShutdownProfilerBindings(vm);
	}

	if (!vm.IsMainThreadVM() || !vm.IsOwnerThread()) {
		lua_pushnil(L);
		lua_setglobal(L, "profiler");
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger,
						"ScriptBind: profiler module export rejected; "
						"target VM is not the main-thread Lua VM");
		return false;
	}

	auto* state = CLOUDENGINE_MEM_NEW(ProfilerBindState);
	state->main_state = L;
	state->owner_thread_id = std::this_thread::get_id();
	lua_pushlightuserdata(L, state);
	lua_setfield(L, LUA_REGISTRYINDEX, kProfilerBindStateKey);

	vm.RegisterModule("profiler", kProfilerFunctions);
	SetProfilerConstants(L);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ScriptBind: profiler module exported "
					"(runtime switches, groups, session and trace APIs)");
	return true;
}

void ShutdownProfilerBindings(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return;

	auto* state = GetProfilerState(L);
	if (!state) return;
	state->shutting_down = true;

	CLOUDENGINE_MEM_DELETE(state);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, kProfilerBindStateKey);
	lua_pushnil(L);
	lua_setglobal(L, "profiler");
}

}  // namespace script
}  // namespace engine
