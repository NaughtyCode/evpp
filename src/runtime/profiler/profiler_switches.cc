#include "runtime/profiler/profiler_switches.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <string>

namespace engine {

namespace {

struct ProfilerEventGroupInfo {
	ProfilerEventGroup group;
	const char* name;
	const char* category;
};

constexpr std::array<ProfilerEventGroupInfo, 15> kProfilerEventGroups{{
	{ProfilerEventGroup::Engine, "engine", "engine"},
	{ProfilerEventGroup::Frame, "frame", "engine"},
	{ProfilerEventGroup::Timer, "timer", "engine.timer"},
	{ProfilerEventGroup::Physics, "physics", "engine.physics"},
	{ProfilerEventGroup::Script, "script", "engine.script"},
	{ProfilerEventGroup::Entity, "entity", "engine.entity"},
	{ProfilerEventGroup::Space, "space", "engine.space"},
	{ProfilerEventGroup::Aoi, "aoi", "engine.aoi"},
	{ProfilerEventGroup::Auth, "auth", "engine.auth"},
	{ProfilerEventGroup::Vm, "vm", "engine.vm"},
	{ProfilerEventGroup::Network, "network", "engine.net"},
	{ProfilerEventGroup::Rpc, "rpc", "engine.rpc"},
	{ProfilerEventGroup::Database, "database", "engine.db"},
	{ProfilerEventGroup::Monitoring, "monitoring", "engine.monitoring"},
	{ProfilerEventGroup::Config, "config", "engine.config"},
}};

std::atomic<bool> g_profiler_runtime_enabled{true};
std::atomic<ProfilerEventGroupMask> g_profiler_enabled_groups{kProfilerAllEventGroups};

std::string NormalizeToken(std::string_view input) {
	auto begin = input.find_first_not_of(" \t\r\n");
	if (begin == std::string_view::npos) return {};
	auto end = input.find_last_not_of(" \t\r\n");

	std::string token(input.substr(begin, end - begin + 1));
	std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return token;
}

ProfilerEventGroupMask ClampGroupMask(ProfilerEventGroupMask mask) noexcept {
	return mask & kProfilerAllEventGroups;
}

}  // namespace

const char* ProfilerEventGroupName(ProfilerEventGroup group) noexcept {
	if (group == ProfilerEventGroup::All) return "all";
	if (group == ProfilerEventGroup::None) return "none";
	for (const auto& info : kProfilerEventGroups) {
		if (info.group == group) return info.name;
	}
	return "unknown";
}

const char* ProfilerEventGroupCategory(ProfilerEventGroup group) noexcept {
	for (const auto& info : kProfilerEventGroups) {
		if (info.group == group) return info.category;
	}
	return "engine";
}

ProfilerEventGroup ProfilerEventGroupFromName(std::string_view name) {
	auto token = NormalizeToken(name);
	if (token.empty() || token == "none" || token == "off" || token == "disabled") {
		return ProfilerEventGroup::None;
	}
	if (token == "all" || token == "*" || token == "on" || token == "enabled") {
		return ProfilerEventGroup::All;
	}

	for (const auto& info : kProfilerEventGroups) {
		if (token == info.name || token == info.category) return info.group;
	}
	if (token == "engine.frame") return ProfilerEventGroup::Frame;
	if (token == "net") return ProfilerEventGroup::Network;
	if (token == "db" || token == "orm" || token == "mongo") return ProfilerEventGroup::Database;
	return ProfilerEventGroup::None;
}

ProfilerEventGroupMask ParseProfilerEventGroupMask(std::string_view names) {
	ProfilerEventGroupMask mask = 0;
	size_t offset = 0;
	while (offset <= names.size()) {
		auto next = names.find_first_of(",;|", offset);
		auto token = next == std::string_view::npos
						 ? names.substr(offset)
						 : names.substr(offset, next - offset);
		auto group = ProfilerEventGroupFromName(token);
		if (group == ProfilerEventGroup::All) return kProfilerAllEventGroups;
		mask |= ProfilerEventGroupBit(group);
		if (next == std::string_view::npos) break;
		offset = next + 1;
	}
	return ClampGroupMask(mask);
}

std::string FormatProfilerEventGroupMask(ProfilerEventGroupMask mask) {
	mask = ClampGroupMask(mask);
	if (mask == 0) return "none";
	if (mask == kProfilerAllEventGroups) return "all";

	std::string result;
	for (const auto& info : kProfilerEventGroups) {
		if ((mask & ProfilerEventGroupBit(info.group)) == 0) continue;
		if (!result.empty()) result += ',';
		result += info.name;
	}
	return result;
}

bool ProfilerRuntimeIsEnabled() noexcept {
	return g_profiler_runtime_enabled.load(std::memory_order_relaxed);
}

void ProfilerRuntimeSetEnabled(bool enabled) noexcept {
	g_profiler_runtime_enabled.store(enabled, std::memory_order_relaxed);
}

ProfilerEventGroupMask ProfilerRuntimeEnabledGroups() noexcept {
	return g_profiler_enabled_groups.load(std::memory_order_relaxed);
}

void ProfilerRuntimeSetEnabledGroups(ProfilerEventGroupMask mask) noexcept {
	g_profiler_enabled_groups.store(ClampGroupMask(mask), std::memory_order_relaxed);
}

void ProfilerRuntimeEnableGroups(ProfilerEventGroupMask mask) noexcept {
	g_profiler_enabled_groups.fetch_or(ClampGroupMask(mask), std::memory_order_relaxed);
}

void ProfilerRuntimeDisableGroups(ProfilerEventGroupMask mask) noexcept {
	g_profiler_enabled_groups.fetch_and(~ClampGroupMask(mask), std::memory_order_relaxed);
}

void ProfilerRuntimeSetGroupEnabled(ProfilerEventGroup group, bool enabled) noexcept {
	if (group == ProfilerEventGroup::All) {
		ProfilerRuntimeSetEnabledGroups(enabled ? kProfilerAllEventGroups : 0);
		return;
	}

	auto bit = ProfilerEventGroupBit(group);
	if (bit == 0) return;
	if (enabled) {
		ProfilerRuntimeEnableGroups(bit);
	} else {
		ProfilerRuntimeDisableGroups(bit);
	}
}

bool ProfilerRuntimeIsGroupEnabled(ProfilerEventGroup group) noexcept {
	auto bit = ClampGroupMask(ProfilerEventGroupBit(group));
	if (bit == 0) return false;
	if (bit == kProfilerAllEventGroups) {
		return ProfilerRuntimeIsEnabled() &&
			   g_profiler_enabled_groups.load(std::memory_order_relaxed) == kProfilerAllEventGroups;
	}
	return ProfilerRuntimeIsEnabled() &&
		   ((g_profiler_enabled_groups.load(std::memory_order_relaxed) & bit) == bit);
}

}  // namespace engine
