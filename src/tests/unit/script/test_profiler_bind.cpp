#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>

#include "log_init.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/profiler/profiler_core.h"
#include "runtime/profiler/profiler_switches.h"
#include "runtime/script/profiler_bind.h"
#include "runtime/script/script_bind.h"
#include "runtime/vm/vm.h"

using namespace engine;

namespace {

struct ProfilerBindFixture {
	ScriptVM vm;
	TimerManager timer_mgr;

	ProfilerBindFixture() {
		ProfilerManager::Get().SetRuntimeEnabled(true);
		ProfilerManager::Get().SetEnabledEventGroups(kProfilerAllEventGroups);
		timer_mgr.initialize();
		script::ExportAll(vm, timer_mgr);
	}

	~ProfilerBindFixture() {
		script::ShutdownRpcBindings(vm);
		script::ShutdownConfigBindings(vm);
		script::ShutdownNetBindings();
		script::ShutdownEntityBindings();
		script::ShutdownTimerBindings(vm);
		script::ShutdownProfilerBindings(vm);
		timer_mgr.shutdown();
		ProfilerManager::Get().SetRuntimeEnabled(true);
		ProfilerManager::Get().SetEnabledEventGroups(kProfilerAllEventGroups);
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_profiler_bind", nullptr, &result);
	}
};

}  // namespace

TEST_CASE("profiler Lua module exports runtime switch APIs on main VM",
		  "[script][profiler_bind]") {
	ProfilerBindFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
profiler.set_runtime_enabled(false)
local runtime_false = profiler.is_runtime_enabled()
profiler.set_runtime_enabled(true)

local mask, names = profiler.set_enabled_groups({ 'physics', 'script' })
local physics_before = profiler.is_group_enabled('physics')
local timer_before = profiler.is_group_enabled('timer')
profiler.disable_groups('physics')
local physics_after = profiler.is_group_enabled('physics')
profiler.enable_groups(profiler.GROUP_TIMER)

local _, group_name = profiler.group_from_category('engine.timer')
local groups = profiler.list_groups()
local status = profiler.status()

return table.concat({
    tostring(runtime_false),
    names,
    tostring(physics_before),
    tostring(timer_before),
    tostring(physics_after),
    group_name,
    tostring(#groups > 0),
    tostring(type(status.cached_trace_size) == 'number'),
}, '|')
)lua",
		result));

	REQUIRE(result == "false|physics,script|true|false|false|timer|true|true");
}

TEST_CASE("profiler Lua module is rejected outside ExportAll main VM path",
		  "[script][profiler_bind]") {
	ScriptVM vm;
	std::string result;

	REQUIRE_FALSE(script::ExportProfiler(vm));
	REQUIRE(vm.DoString("return tostring(profiler == nil)", "test_profiler_bind", nullptr, &result));
	REQUIRE(result == "true");
}

TEST_CASE("profiler Lua module rejects invalid runtime control arguments",
		  "[script][profiler_bind]") {
	ProfilerBindFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
local checks = {}

checks[#checks + 1] = tostring(pcall(profiler.set_runtime_enabled) == false)
checks[#checks + 1] = tostring(pcall(profiler.set_runtime_enabled, 'yes') == false)
checks[#checks + 1] = tostring(pcall(profiler.set_enabled_groups, 'physics,missing') == false)
checks[#checks + 1] = tostring(pcall(profiler.set_enabled_groups, -1) == false)
checks[#checks + 1] = tostring(pcall(profiler.set_enabled_groups, profiler.GROUP_ALL + 1) == false)
local combined_group = profiler.GROUP_PHYSICS + profiler.GROUP_SCRIPT
checks[#checks + 1] = tostring(pcall(profiler.set_group_enabled, combined_group, false) == false)
checks[#checks + 1] = tostring(pcall(profiler.set_group_enabled, 'physics') == false)
checks[#checks + 1] = tostring(pcall(profiler.initialize, { runtime_enabled = 'yes' }) == false)
checks[#checks + 1] = tostring(pcall(profiler.initialize, { buffer_size_kb = -1 }) == false)

return table.concat(checks, ',')
)lua",
		result));

	REQUIRE(result == "true,true,true,true,true,true,true,true,true");
}

TEST_CASE("profiler Lua APIs reject calls from non-owner thread", "[script][profiler_bind]") {
	ProfilerBindFixture f;
	std::string error;
	bool ok = true;

	std::thread worker([&] {
		ok = f.vm.DoString("return profiler.is_runtime_enabled()",
						   "test_profiler_bind_wrong_thread",
						   &error);
	});
	worker.join();

	REQUIRE_FALSE(ok);
	REQUIRE(error.find("profiler: API must be called from main thread") != std::string::npos);
}
