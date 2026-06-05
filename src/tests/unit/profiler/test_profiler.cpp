#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "runtime/profiler/profiler_core.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/profiler/profiler_macros.h"
#include "runtime/profiler/profiler_switches.h"

using namespace engine;

namespace {

struct TempDir {
	std::filesystem::path path;

	explicit TempDir(const std::string& name)
		: path(std::filesystem::temp_directory_path() / name) {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		std::filesystem::create_directories(path, ec);
	}

	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	std::string file(const std::string& name) const {
		return (path / name).string();
	}
};

void EmitProfilerTestEvents() {
	ENGINE_PROFILE_SCOPE("engine", "ProfilerUnitScope", "value", 7);
	ENGINE_PROFILE_INSTANT("engine", "ProfilerUnitInstant", "value", 11);
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

ProfilerConfig MakeProfilerConfig(const std::string& output_path) {
	ProfilerConfig cfg;
	cfg.output_path = output_path;
	cfg.buffer_size_kb = 1024;
	cfg.flush_interval_ms = 1;
	return cfg;
}

}  // namespace

TEST_CASE("ProfilerManager caches trace after StopSession for later save", "[profiler]") {
	auto& profiler = ProfilerManager::Get();
	profiler.Shutdown();
	profiler.ClearCachedTrace();

#ifdef ENGINE_PROFILER_ENABLED
	TempDir tmp("profiler_cache_test");
	auto cfg = MakeProfilerConfig(tmp.file("trace.perfetto-trace"));

	REQUIRE(profiler.Initialize(cfg));
	REQUIRE(profiler.IsInitialized());
	REQUIRE(profiler.StartSession());
	REQUIRE(profiler.IsActive());
	REQUIRE(profiler.SaveTrace().empty());

	EmitProfilerTestEvents();
	profiler.Flush();
	profiler.StopSession();

	REQUIRE_FALSE(profiler.IsActive());
	auto trace = profiler.ReadTrace();
	REQUIRE_FALSE(trace.empty());
	REQUIRE(profiler.CachedTraceSize() == trace.size());

	auto exact_path = tmp.file("nested/exact.perfetto-trace");
	REQUIRE(profiler.SaveTraceExact(exact_path));
	REQUIRE(std::filesystem::is_regular_file(exact_path));
	REQUIRE(std::filesystem::file_size(exact_path) == trace.size());

	auto stamped_path = profiler.SaveTrace();
	REQUIRE_FALSE(stamped_path.empty());
	REQUIRE(std::filesystem::is_regular_file(stamped_path));
	REQUIRE(profiler.LastSavedPath() == stamped_path);

	profiler.Shutdown();
#else
	REQUIRE_FALSE(ProfilerManager::IsEnabled());
	REQUIRE_FALSE(profiler.IsInitialized());
	REQUIRE_FALSE(profiler.StartSession());
	REQUIRE(profiler.ReadTrace().empty());
	REQUIRE(profiler.SaveTrace().empty());
#endif
}

TEST_CASE("ProfilerManager auto-saves exact output path when requested", "[profiler]") {
	auto& profiler = ProfilerManager::Get();
	profiler.Shutdown();
	profiler.ClearCachedTrace();

#ifdef ENGINE_PROFILER_ENABLED
	TempDir tmp("profiler_autosave_test");
	auto exact_path = tmp.file("auto/trace.perfetto-trace");
	auto cfg = MakeProfilerConfig(exact_path);
	cfg.write_into_file = true;

	REQUIRE(profiler.Initialize(cfg));
	REQUIRE(profiler.StartSession());
	EmitProfilerTestEvents();
	profiler.StopSession();

	REQUIRE(std::filesystem::is_regular_file(exact_path));
	REQUIRE(std::filesystem::file_size(exact_path) > 0);
	REQUIRE(profiler.LastSavedPath() == exact_path);

	profiler.Shutdown();
#else
	REQUIRE_FALSE(ProfilerManager::IsEnabled());
#endif
}

TEST_CASE("Profiler runtime switches parse and gate event arguments", "[profiler]") {
	auto& profiler = ProfilerManager::Get();
	profiler.SetRuntimeEnabled(true);
	profiler.SetEnabledEventGroups(kProfilerAllEventGroups);

	REQUIRE(ParseProfilerEventGroupMask("physics,script,engine.aoi") ==
			(ProfilerEventGroupBit(ProfilerEventGroup::Physics) |
			 ProfilerEventGroupBit(ProfilerEventGroup::Script) |
			 ProfilerEventGroupBit(ProfilerEventGroup::Aoi)));
	REQUIRE(FormatProfilerEventGroupMask(ProfilerEventGroupBit(ProfilerEventGroup::Physics) |
										 ProfilerEventGroupBit(ProfilerEventGroup::Script)) ==
			"physics,script");
	REQUIRE(ProfilerEventGroupFromCategory("engine.physics") == ProfilerEventGroup::Physics);
	REQUIRE(ProfilerEventGroupFromCategory("engine.physics.step") ==
			ProfilerEventGroup::Physics);
	REQUIRE(ProfilerEventGroupFromCategory("engine.physics2") == ProfilerEventGroup::Engine);
	REQUIRE(ProfilerEventGroupFromCategory("engine.network") == ProfilerEventGroup::Engine);
	REQUIRE(ProfilerEventGroupFromCategory("engine.frame") == ProfilerEventGroup::Frame);
	REQUIRE(ProfilerEventGroupFromCategory("engine") == ProfilerEventGroup::Engine);

	profiler.SetEventGroupEnabled(ProfilerEventGroup::Physics, false);
	REQUIRE_FALSE(profiler.IsEventGroupEnabled(ProfilerEventGroup::Physics));
	REQUIRE(profiler.IsEventGroupEnabled(ProfilerEventGroup::Script));
	REQUIRE_FALSE(profiler.IsEventGroupEnabled(ProfilerEventGroup::All));

	int evaluated = 0;
	ENGINE_PROFILE_INSTANT("engine.physics", "DisabledPhysics", "value", ++evaluated);
	REQUIRE(evaluated == 0);

	ENGINE_PROFILE_BEGIN("engine.physics", "DisabledPhysicsRange", "value", ++evaluated);
	ENGINE_PROFILE_END("engine.physics");
	REQUIRE(evaluated == 0);

	profiler.SetRuntimeEnabled(false);
	ENGINE_PROFILE_INSTANT("engine.script", "DisabledRuntime", "value", ++evaluated);
	REQUIRE(evaluated == 0);

	profiler.SetRuntimeEnabled(true);
	profiler.SetEnabledEventGroups(0);
	ENGINE_PROFILE_SLOW_FRAME(++evaluated, 0);
	REQUIRE(evaluated == 0);

	profiler.SetEnabledEventGroups(kProfilerAllEventGroups);
	REQUIRE(profiler.IsEventGroupEnabled(ProfilerEventGroup::All));
	REQUIRE(profiler.EnabledEventGroups() == kProfilerAllEventGroups);

#ifndef ENGINE_PROFILER_ENABLED
	ProfilerConfig cfg;
	cfg.runtime_enabled = false;
	cfg.enabled_event_groups = ProfilerEventGroupBit(ProfilerEventGroup::Script);
	REQUIRE_FALSE(profiler.Initialize(cfg));
	REQUIRE_FALSE(profiler.IsInitialized());
	REQUIRE_FALSE(profiler.IsRuntimeEnabled());
	REQUIRE(profiler.EnabledEventGroups() == ProfilerEventGroupBit(ProfilerEventGroup::Script));

	int disabled_command_evaluated = 0;
	ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE(++disabled_command_evaluated);
	REQUIRE(disabled_command_evaluated == 0);
#endif
}
