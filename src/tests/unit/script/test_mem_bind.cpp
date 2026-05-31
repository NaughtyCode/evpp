#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "log_init.h"
#include "runtime/core/mem/mem.h"
#include "runtime/script/mem_bind.h"
#include "runtime/vm/vm.h"

#if defined(ENGINE_MEM_STATS_ENABLED)
#include "runtime/core/mem/mem_stats.h"
#endif

TEST_CASE("mem binding reflects build-time memory stats configuration", "[mem_bind][module]") {
#if defined(ENGINE_MEM_STATS_ENABLED)
	engine::mem::MemStats::Instance().Reset();

	engine::ScriptVM vm;
	engine::script::ExportMem(vm);

	auto* value = CLOUDENGINE_MEM_NEW(int, 42);
	CLOUDENGINE_MEM_DELETE(value);

	std::string result;
	REQUIRE(vm.DoString(
		R"lua(
local stats = mem.get_stats()
return tostring(mem.is_enabled()) .. ',' ..
       tostring(stats.totals.alloc_count >= 1) .. ',' ..
       tostring(stats.totals.free_count >= 1) .. ',' ..
       type(stats.by_operation.new) .. ',' ..
       type(stats.size_buckets.lt_64)
)lua",
		"test_mem_bind",
		nullptr,
		&result));
	REQUIRE(result == "true,true,true,table,table");
#else
	SUCCEED("ENGINE_MEM_STATS_ENABLED is off in this build");
#endif
}

TEST_CASE("mem.dump_stats writes a JSON snapshot when memory stats are enabled", "[mem_bind][dump]") {
#if defined(ENGINE_MEM_STATS_ENABLED)
	engine::mem::MemStats::Instance().Reset();

	engine::ScriptVM vm;
	engine::script::ExportMem(vm);

	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	auto path = std::filesystem::temp_directory_path() /
				("evpp_mem_stats_" + std::to_string(stamp) + ".json");
	std::error_code ec;
	std::filesystem::remove(path, ec);

	vm.SetGlobal<std::string_view>("mem_stats_path", path.string());
	std::string result;
	REQUIRE(vm.DoString(
		"return tostring(mem.dump_stats(mem_stats_path))",
		"test_mem_bind",
		nullptr,
		&result));
	REQUIRE(result == "true");

	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	const std::string content((std::istreambuf_iterator<char>(input)),
							  std::istreambuf_iterator<char>());
	REQUIRE(content.find("\"totals\"") != std::string::npos);
	REQUIRE(content.find("\"size_buckets\"") != std::string::npos);

	std::filesystem::remove(path, ec);
#else
	SUCCEED("ENGINE_MEM_STATS_ENABLED is off in this build");
#endif
}
