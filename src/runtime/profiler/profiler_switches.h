#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "runtime/core/engine_api.h"

namespace engine {

using ProfilerEventGroupMask = uint64_t;

enum class ProfilerEventGroup : ProfilerEventGroupMask {
	None = 0,
	Engine = 1ull << 0,
	Frame = 1ull << 1,
	Timer = 1ull << 2,
	Physics = 1ull << 3,
	Script = 1ull << 4,
	Entity = 1ull << 5,
	Space = 1ull << 6,
	Aoi = 1ull << 7,
	Auth = 1ull << 8,
	Vm = 1ull << 9,
	Network = 1ull << 10,
	Rpc = 1ull << 11,
	Database = 1ull << 12,
	Monitoring = 1ull << 13,
	Config = 1ull << 14,
	All = (1ull << 15) - 1ull
};

inline constexpr ProfilerEventGroupMask kProfilerAllEventGroups =
	static_cast<ProfilerEventGroupMask>(ProfilerEventGroup::All);

constexpr ProfilerEventGroupMask ProfilerEventGroupBit(ProfilerEventGroup group) noexcept {
	return static_cast<ProfilerEventGroupMask>(group);
}

inline constexpr bool ProfilerCategoryStartsWith(std::string_view value,
												 std::string_view prefix) noexcept {
	return value == prefix ||
		   (value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix &&
			value[prefix.size()] == '.');
}

inline ProfilerEventGroup ProfilerEventGroupFromCategory(std::string_view category) noexcept {
	if (ProfilerCategoryStartsWith(category, "engine.entity")) return ProfilerEventGroup::Entity;
	if (ProfilerCategoryStartsWith(category, "engine.space")) return ProfilerEventGroup::Space;
	if (ProfilerCategoryStartsWith(category, "engine.aoi")) return ProfilerEventGroup::Aoi;
	if (ProfilerCategoryStartsWith(category, "engine.auth")) return ProfilerEventGroup::Auth;
	if (ProfilerCategoryStartsWith(category, "engine.script")) return ProfilerEventGroup::Script;
	if (ProfilerCategoryStartsWith(category, "engine.physics")) return ProfilerEventGroup::Physics;
	if (ProfilerCategoryStartsWith(category, "engine.timer")) return ProfilerEventGroup::Timer;
	if (ProfilerCategoryStartsWith(category, "engine.vm")) return ProfilerEventGroup::Vm;
	if (ProfilerCategoryStartsWith(category, "engine.frame")) return ProfilerEventGroup::Frame;
	if (ProfilerCategoryStartsWith(category, "engine.net")) return ProfilerEventGroup::Network;
	if (ProfilerCategoryStartsWith(category, "engine.rpc")) return ProfilerEventGroup::Rpc;
	if (ProfilerCategoryStartsWith(category, "engine.db")) return ProfilerEventGroup::Database;
	if (ProfilerCategoryStartsWith(category, "engine.monitoring")) {
		return ProfilerEventGroup::Monitoring;
	}
	if (ProfilerCategoryStartsWith(category, "engine.config")) return ProfilerEventGroup::Config;
	return ProfilerEventGroup::Engine;
}

CLOUD_ENGINE_API const char* ProfilerEventGroupName(ProfilerEventGroup group) noexcept;
CLOUD_ENGINE_API const char* ProfilerEventGroupCategory(ProfilerEventGroup group) noexcept;
CLOUD_ENGINE_API ProfilerEventGroup ProfilerEventGroupFromName(std::string_view name);
CLOUD_ENGINE_API ProfilerEventGroupMask ParseProfilerEventGroupMask(std::string_view names);
CLOUD_ENGINE_API std::string FormatProfilerEventGroupMask(ProfilerEventGroupMask mask);

CLOUD_ENGINE_API bool ProfilerRuntimeIsEnabled() noexcept;
CLOUD_ENGINE_API void ProfilerRuntimeSetEnabled(bool enabled) noexcept;
CLOUD_ENGINE_API ProfilerEventGroupMask ProfilerRuntimeEnabledGroups() noexcept;
CLOUD_ENGINE_API void ProfilerRuntimeSetEnabledGroups(ProfilerEventGroupMask mask) noexcept;
CLOUD_ENGINE_API void ProfilerRuntimeEnableGroups(ProfilerEventGroupMask mask) noexcept;
CLOUD_ENGINE_API void ProfilerRuntimeDisableGroups(ProfilerEventGroupMask mask) noexcept;
CLOUD_ENGINE_API void ProfilerRuntimeSetGroupEnabled(ProfilerEventGroup group,
													 bool enabled) noexcept;
CLOUD_ENGINE_API bool ProfilerRuntimeIsGroupEnabled(ProfilerEventGroup group) noexcept;

}  // namespace engine
