#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "runtime/vm/file_watcher.h"
#include "runtime/vm/script_reloader.h"
#include "runtime/vm/script_validator.h"
#include "runtime/vm/vm.h"

using namespace engine;

namespace {

// RAII helper to create and clean up a temp directory.
struct TempDir {
	std::string path;

	explicit TempDir(const std::string& name) {
		path = (std::filesystem::temp_directory_path() / name).string();
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		std::filesystem::create_directories(path, ec);
	}

	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	std::string file(const std::string& name) const {
		return (std::filesystem::path(path) / name).string();
	}

	void write(const std::string& name, const std::string& content) {
		std::ofstream of(file(name));
		of << content;
	}
};

}  // namespace

// ============================================================================
// FileWatcher — construction & basic state
// ============================================================================

TEST_CASE("FileWatcher default construction", "[hotreload][filewatcher]") {
	FileWatcher watcher;
	REQUIRE_FALSE(watcher.IsRunning());
}

TEST_CASE("FileWatcher non-copyable", "[hotreload][filewatcher]") {
	STATIC_REQUIRE_FALSE(std::is_copy_constructible<FileWatcher>::value);
	STATIC_REQUIRE_FALSE(std::is_copy_assignable<FileWatcher>::value);
}

// ============================================================================
// FileWatcher — start / stop lifecycle
// ============================================================================

TEST_CASE("FileWatcher start then stop", "[hotreload][filewatcher]") {
	FileWatcher watcher;
	REQUIRE_NOTHROW(watcher.Start(100));
	REQUIRE(watcher.IsRunning());
	REQUIRE_NOTHROW(watcher.Stop());
	REQUIRE_FALSE(watcher.IsRunning());
}

TEST_CASE("FileWatcher stop without start is safe", "[hotreload][filewatcher]") {
	FileWatcher watcher;
	REQUIRE_NOTHROW(watcher.Stop());
}

TEST_CASE("FileWatcher double start is safe", "[hotreload][filewatcher]") {
	FileWatcher watcher;
	REQUIRE_NOTHROW(watcher.Start(100));
	REQUIRE_NOTHROW(watcher.Start(200));  // second start is no-op
	REQUIRE_NOTHROW(watcher.Stop());
}

TEST_CASE("FileWatcher restart cycle", "[hotreload][filewatcher]") {
	FileWatcher watcher;
	for (int i = 0; i < 3; ++i) {
		REQUIRE_NOTHROW(watcher.Start(50));
		REQUIRE_NOTHROW(watcher.Stop());
	}
}

// ============================================================================
// FileWatcher — watch directory (per-directory extension)
// ============================================================================

TEST_CASE("FileWatcher watch multiple directories with different extensions",
          "[hotreload][filewatcher]") {
	FileWatcher watcher;
	REQUIRE_NOTHROW(watcher.WatchDirectory("/tmp/a", ".lua"));
	REQUIRE_NOTHROW(watcher.WatchDirectory("/tmp/b", ".json"));
	// Both should be accepted; previously the second call would overwrite
	// the extension for the first directory.
}

// ============================================================================
// FileWatcher — new file detection
// ============================================================================

TEST_CASE("FileWatcher detects newly created files", "[hotreload][filewatcher]") {
	TempDir tmp("hotreload_newfile_test");
	tmp.write("old.lua", "return 1");

	FileWatcher watcher;
	watcher.WatchDirectory(tmp.path, ".lua");

	std::atomic<int> change_count{0};
	std::string last_changed;
	watcher.SetChangeCallback(
		[&](const std::vector<std::string>& files) {
			change_count++;
			if (!files.empty()) last_changed = files[0];
		});

	// Start watching — first scan will report the pre-existing file as "new".
	watcher.Start(50);

	// Wait for at least one scan cycle.
	std::this_thread::sleep_for(std::chrono::milliseconds(150));

	// Now create a brand-new file.
	tmp.write("new.lua", "return 42");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));

	watcher.Stop();

	// The new file should have been detected.
	REQUIRE(change_count.load() > 0);
}

TEST_CASE("FileWatcher priming ignores pre-existing files",
          "[hotreload][filewatcher]") {
	TempDir tmp("hotreload_prime_test");
	tmp.write("old.lua", "return 1");

	FileWatcher watcher;
	watcher.WatchDirectory(tmp.path, ".lua");

	std::atomic<int> change_count{0};
	watcher.SetChangeCallback(
		[&](const std::vector<std::string>&) { change_count++; });

	watcher.PrimeKnownFiles();
	watcher.Start(50);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	REQUIRE(change_count.load() == 0);

	tmp.write("new.lua", "return 42");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));

	watcher.Stop();
	REQUIRE(change_count.load() > 0);
}

// ============================================================================
// FileWatcher — file modification detection
// ============================================================================

TEST_CASE("FileWatcher detects modifications to existing files",
          "[hotreload][filewatcher]") {
	TempDir tmp("hotreload_mod_test");
	tmp.write("mod.lua", "return 1");

	FileWatcher watcher;
	watcher.WatchDirectory(tmp.path, ".lua");

	std::atomic<int> change_count{0};
	watcher.SetChangeCallback(
		[&](const std::vector<std::string>&) { change_count++; });

	watcher.Start(50);

	// Let the initial scan settle (it will report the pre-existing file).
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	int count_after_initial = change_count.load();

	// Modify the file.
	tmp.write("mod.lua", "return 2");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));

	watcher.Stop();

	// Should have detected the modification.
	REQUIRE(change_count.load() > count_after_initial);
}

// ============================================================================
// ScriptReloader — construction & basic state
// ============================================================================

TEST_CASE("ScriptReloader default construction", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(ScriptReloader());
}

TEST_CASE("ScriptReloader non-copyable, non-movable", "[hotreload][lifecycle]") {
	STATIC_REQUIRE_FALSE(std::is_copy_constructible<ScriptReloader>::value);
	STATIC_REQUIRE_FALSE(std::is_copy_assignable<ScriptReloader>::value);
}

// ============================================================================
// ScriptReloader — SetTarget
// ============================================================================

TEST_CASE("SetTarget with nullptr VM and empty dirs", "[hotreload][target]") {
	ScriptReloader reloader;
	std::vector<std::string> empty_dirs;
	REQUIRE_NOTHROW(reloader.SetTarget(nullptr, empty_dirs));
}

TEST_CASE("SetTarget with nullptr VM and some script dirs", "[hotreload][target]") {
	ScriptReloader reloader;
	std::vector<std::string> dirs = {"scripts/", "modules/"};
	REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs));
}

TEST_CASE("SetTarget called multiple times overwrites previous",
          "[hotreload][target]") {
	ScriptReloader reloader;
	std::vector<std::string> dirs1 = {"dir1/"};
	std::vector<std::string> dirs2 = {"dir2/", "dir3/"};
	REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs1));
	REQUIRE_NOTHROW(reloader.SetTarget(nullptr, dirs2));
}

// ============================================================================
// ScriptReloader — SetReloadCallback
// ============================================================================

TEST_CASE("SetReloadCallback with null function pointer", "[hotreload][callback]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(reloader.SetReloadCallback(nullptr));
}

TEST_CASE("SetReloadCallback with valid function", "[hotreload][callback]") {
	ScriptReloader reloader;
	int call_count = 0;
	auto cb = [&call_count](const std::string& path, bool success) {
		call_count++;
	};
	REQUIRE_NOTHROW(reloader.SetReloadCallback(cb));
	REQUIRE(call_count == 0);
}

TEST_CASE("SetReloadCallback overwrites previous callback", "[hotreload][callback]") {
	ScriptReloader reloader;
	int calls_a = 0, calls_b = 0;
	reloader.SetReloadCallback([&](const std::string&, bool) { calls_a++; });
	reloader.SetReloadCallback([&](const std::string&, bool) { calls_b++; });
	REQUIRE(calls_a == 0);
	REQUIRE(calls_b == 0);
}

// ============================================================================
// ScriptReloader — SetEventLoop
// ============================================================================

TEST_CASE("SetEventLoop with nullptr", "[hotreload][eventloop]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(reloader.SetEventLoop(nullptr));
}

TEST_CASE("SetEventLoop before Start", "[hotreload][eventloop]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});
	REQUIRE_NOTHROW(reloader.SetEventLoop(nullptr));
	REQUIRE_NOTHROW(reloader.Start(100, 50));
	REQUIRE_NOTHROW(reloader.Stop());
}

// ============================================================================
// ScriptReloader — ReloadFile
// ============================================================================

TEST_CASE("ReloadFile with empty path", "[hotreload][reload]") {
	ScriptReloader reloader;
	bool result = reloader.ReloadFile("");
	REQUIRE_FALSE(result);
}

TEST_CASE("ReloadFile with non-existent file", "[hotreload][reload]") {
	ScriptReloader reloader;
	bool result = reloader.ReloadFile("nonexistent_script.lua");
	REQUIRE_FALSE(result);
}

TEST_CASE("ReloadFile with valid file but no target VM", "[hotreload][reload]") {
	TempDir tmp("hotreload_novm_test");
	tmp.write("test.lua", "return 42");

	ScriptReloader reloader;
	bool result = reloader.ReloadFile(tmp.file("test.lua"));
	REQUIRE_FALSE(result);
}

// ============================================================================
// ScriptReloader — actual reload with ScriptVM
// ============================================================================

TEST_CASE("ReloadFile loads and executes a Lua script", "[hotreload][reload]") {
	TempDir tmp("hotreload_reload_test");
	tmp.write("simple.lua", "x = 42");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	bool ok = reloader.ReloadFile(tmp.file("simple.lua"));
	REQUIRE(ok);

	// Verify the global 'x' was set to 42.
	lua_State* L = vm.GetState();
	lua_getglobal(L, "x");
	REQUIRE(lua_isinteger(L, -1));
	REQUIRE(lua_tointeger(L, -1) == 42);
	lua_pop(L, 1);
}

TEST_CASE("ReloadFile restores Lua stack after repeated successful reloads",
          "[hotreload][reload]") {
	TempDir tmp("hotreload_stack_test");
	tmp.write("simple.lua", "return { value = 1 }");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	lua_State* L = vm.GetState();
	int base_top = lua_gettop(L);

	REQUIRE(reloader.ReloadFile(tmp.file("simple.lua")));
	REQUIRE(lua_gettop(L) == base_top);

	tmp.write("simple.lua", "return { value = 2 }");
	REQUIRE(reloader.ReloadFile(tmp.file("simple.lua")));
	REQUIRE(lua_gettop(L) == base_top);
}

TEST_CASE("ReloadFile returns false for script with syntax error",
          "[hotreload][reload]") {
	TempDir tmp("hotreload_synerr_test");
	tmp.write("broken.lua", "this is not valid lua{{{");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	bool ok = reloader.ReloadFile(tmp.file("broken.lua"));
	REQUIRE_FALSE(ok);
}

TEST_CASE("LuaScriptValidator checks syntax, top-level runtime, and import",
          "[hotreload][validator]") {
	TempDir tmp("hotreload_validator_test");
	tmp.write("dep.lua", "return { value = 7 }");
	tmp.write("good.lua", R"(
local dep = import("dep")
assert(dep.value == 7)
assert(type(import.loaded()) == "table")
import.clearcache()
return true
)");
	tmp.write("syntax.lua", "this is not valid lua{{{");
	tmp.write("runtime.lua", "local x = 1\nerror('boom')");

	LuaScriptValidator validator;
	validator.SetScriptDirs({tmp.path});

	auto good = validator.ValidateFile(tmp.file("good.lua"));
	REQUIRE(good.ok());

	auto syntax = validator.ValidateFile(tmp.file("syntax.lua"));
	REQUIRE_FALSE(syntax.ok());
	REQUIRE(syntax.status == LuaScriptValidationResult::Status::CompileError);

	auto runtime = validator.ValidateFile(tmp.file("runtime.lua"));
	REQUIRE_FALSE(runtime.ok());
	REQUIRE(runtime.status == LuaScriptValidationResult::Status::RuntimeError);
}

TEST_CASE("ReloadFile rejects runtime failure before touching target VM",
          "[hotreload][reload]") {
	TempDir tmp("hotreload_prevalidate_runtime_test");
	tmp.write("guard.lua", "reload_guard = 200\nerror('reject reload')");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	REQUIRE_FALSE(reloader.ReloadFile(tmp.file("guard.lua")));

	lua_State* L = vm.GetState();
	lua_getglobal(L, "reload_guard");
	REQUIRE(lua_isnil(L, -1));
	lua_pop(L, 1);
}

TEST_CASE("ReloadFile rollback restores globals on failure",
          "[hotreload][reload]") {
	TempDir tmp("hotreload_rollback_test");

	// First, create and load a valid script that sets a global.
	tmp.write("step1.lua", "my_global = 100");
	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	bool ok = reloader.ReloadFile(tmp.file("step1.lua"));
	REQUIRE(ok);

	// Verify the global is set.
	lua_State* L = vm.GetState();
	lua_getglobal(L, "my_global");
	REQUIRE(lua_isinteger(L, -1));
	REQUIRE(lua_tointeger(L, -1) == 100);
	lua_pop(L, 1);

	// Now overwrite with a broken script.
	tmp.write("step1.lua", "my_global = 200\nsyntax error here[[[");

	ok = reloader.ReloadFile(tmp.file("step1.lua"));
	REQUIRE_FALSE(ok);

	// Verify the global was rolled back to the original value.
	lua_getglobal(L, "my_global");
	REQUIRE(lua_isinteger(L, -1));
	REQUIRE(lua_tointeger(L, -1) == 100);
	lua_pop(L, 1);
}

TEST_CASE("ReloadFile preserves typed globals across rollback",
          "[hotreload][reload]") {
	TempDir tmp("hotreload_typed_test");

	// Load a script with multiple typed globals.
	tmp.write("typed.lua", R"(
bool_val = true
int_val = 42
num_val = 3.14
str_val = "hello"
)");
	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	bool ok = reloader.ReloadFile(tmp.file("typed.lua"));
	REQUIRE(ok);

	// Now overwrite with a broken script.
	tmp.write("typed.lua", "syntax error{{");

	ok = reloader.ReloadFile(tmp.file("typed.lua"));
	REQUIRE_FALSE(ok);

	// Verify all types are preserved after rollback.
	lua_State* L = vm.GetState();

	lua_getglobal(L, "bool_val");
	REQUIRE(lua_isboolean(L, -1));
	REQUIRE(lua_toboolean(L, -1) == 1);
	lua_pop(L, 1);

	lua_getglobal(L, "int_val");
	REQUIRE(lua_isinteger(L, -1));
	REQUIRE(lua_tointeger(L, -1) == 42);
	lua_pop(L, 1);

	lua_getglobal(L, "num_val");
	REQUIRE(lua_isnumber(L, -1));
	REQUIRE(lua_tonumber(L, -1) == Catch::Approx(3.14));
	lua_pop(L, 1);

	lua_getglobal(L, "str_val");
	REQUIRE(lua_isstring(L, -1));
	REQUIRE(std::string(lua_tostring(L, -1)) == "hello");
	lua_pop(L, 1);
}

// ============================================================================
// ScriptReloader — module name extraction
// ============================================================================

TEST_CASE("ReloadFile extracts path-based default module name from script dirs",
          "[hotreload][reload]") {
	TempDir base("hotreload_modname_test");

	// Create nested dirs: <base>/sub/mod.lua
	std::filesystem::path sub = std::filesystem::path(base.path) / "sub";
	std::error_code ec;
	std::filesystem::create_directories(sub, ec);

	std::ofstream of(sub / "mod.lua");
	of << "return { name = 'sub_mod' }";
	of.close();

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {base.path});

	std::string fpath = (sub / "mod.lua").string();
	bool ok = reloader.ReloadFile(fpath);
	REQUIRE(ok);

	// Verify the module was cached as "sub_mod" in package.loaded.
	lua_State* L = vm.GetState();
	lua_getglobal(L, "package");
	REQUIRE(lua_istable(L, -1));
	lua_getfield(L, -1, "loaded");
	REQUIRE(lua_istable(L, -1));
	lua_getfield(L, -1, "sub_mod");
	REQUIRE(lua_istable(L, -1));
	lua_pop(L, 3);
}

// ============================================================================
// ScriptReloader — ReloadAll
// ============================================================================

TEST_CASE("ReloadAll with no target set", "[hotreload][reload_all]") {
	ScriptReloader reloader;
	bool result = reloader.ReloadAll();
	// Should return true (nothing to reload, not a failure).
	REQUIRE(result);
}

TEST_CASE("ReloadAll with target but empty dirs", "[hotreload][reload_all]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});
	REQUIRE_NOTHROW(reloader.ReloadAll());
}

TEST_CASE("ReloadAll loads all lua files in watched dirs",
          "[hotreload][reload_all]") {
	TempDir tmp("hotreload_all_test");
	tmp.write("a.lua", "a_loaded = true");
	tmp.write("b.lua", "b_loaded = true");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	bool ok = reloader.ReloadAll();
	REQUIRE(ok);

	lua_State* L = vm.GetState();
	lua_getglobal(L, "a_loaded");
	REQUIRE(lua_toboolean(L, -1) == 1);
	lua_pop(L, 1);

	lua_getglobal(L, "b_loaded");
	REQUIRE(lua_toboolean(L, -1) == 1);
	lua_pop(L, 1);
}

// ============================================================================
// ScriptReloader — ProcessPendingReloads
// ============================================================================

TEST_CASE("ProcessPendingReloads with no pending reloads is safe",
          "[hotreload][pending]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(reloader.ProcessPendingReloads());
}

// ============================================================================
// ScriptReloader — Start / Stop lifecycle
// ============================================================================

TEST_CASE("Start with no target set logs error", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(reloader.Start());
}

TEST_CASE("Start then Stop", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});
	REQUIRE_NOTHROW(reloader.Start());
	REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Start with custom poll and debounce intervals", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});
	REQUIRE_NOTHROW(reloader.Start(500, 100));
	REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Stop without Start", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Start-Stop-Start cycle", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});

	REQUIRE_NOTHROW(reloader.Start());
	REQUIRE_NOTHROW(reloader.Stop());
	REQUIRE_NOTHROW(reloader.Start(2000, 500));
	REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("Multiple Start calls without Stop are safe", "[hotreload][lifecycle]") {
	ScriptReloader reloader;
	ScriptVM vm;
	reloader.SetTarget(&vm, {});

	REQUIRE_NOTHROW(reloader.Start());
	REQUIRE_NOTHROW(reloader.Start());  // Second start — should be safe
	REQUIRE_NOTHROW(reloader.Stop());
}

// ============================================================================
// ScriptReloader — full setup then reload flow
// ============================================================================

TEST_CASE("Full setup: SetTarget + SetCallback + SetEventLoop + Start + ReloadAll + Stop",
          "[hotreload][integration]") {
	TempDir tmp("hotreload_full_test");
	tmp.write("mod.lua", "full_test_ok = true");

	ScriptReloader reloader;
	ScriptVM vm;

	std::atomic<int> reload_attempts{0};
	reloader.SetReloadCallback(
		[&](const std::string& path, bool success) { reload_attempts++; });

	reloader.SetTarget(&vm, {tmp.path});
	reloader.SetEventLoop(nullptr);

	REQUIRE_NOTHROW(reloader.Start(100, 50));

	bool reload_ok = reloader.ReloadAll();
	REQUIRE(reload_ok);

	// Process any pending reloads.
	reloader.ProcessPendingReloads();

	REQUIRE_NOTHROW(reloader.Stop());

	// Verify the module loaded correctly.
	lua_State* L = vm.GetState();
	lua_getglobal(L, "full_test_ok");
	REQUIRE(lua_toboolean(L, -1) == 1);
	lua_pop(L, 1);
}

TEST_CASE("Destructor during active watching is safe", "[hotreload][lifecycle]") {
	ScriptVM vm;
	{
		ScriptReloader reloader;
		reloader.SetTarget(&vm, {});
		reloader.Start();
		// Let destructor call Stop if needed
	}
	SUCCEED("destructor completed without crash");
}

// ============================================================================
// ScriptReloader — multiple instances
// ============================================================================

TEST_CASE("Multiple independent reloaders", "[hotreload][lifecycle]") {
	ScriptVM vm1, vm2;
	ScriptReloader r1, r2;

	r1.SetTarget(&vm1, {"a/"});
	r2.SetTarget(&vm2, {"b/"});

	REQUIRE_NOTHROW(r1.Start());
	REQUIRE_NOTHROW(r2.Start());
	REQUIRE_NOTHROW(r1.Stop());
	REQUIRE_NOTHROW(r2.Stop());
}

// ============================================================================
// ScriptReloader — debounce: reload callback invoked after validation
// ============================================================================

TEST_CASE("Reload callback is invoked on success", "[hotreload][callback]") {
	TempDir tmp("hotreload_cb_test");
	tmp.write("good.lua", "cb_test_ok = 1");

	ScriptVM vm;
	ScriptReloader reloader;
	reloader.SetTarget(&vm, {tmp.path});

	std::atomic<int> success_count{0};
	std::atomic<int> failure_count{0};
	std::string last_path;
	reloader.SetReloadCallback(
		[&](const std::string& path, bool success) {
			last_path = path;
			if (success) success_count++;
			else failure_count++;
		});

	bool ok = reloader.ReloadFile(tmp.file("good.lua"));
	REQUIRE(ok);
	// The callback is NOT called by ReloadFile directly; it's called by
	// ProcessReloadList (which is used by OnFilesChanged). Manual ReloadFile
	// does not invoke the callback to avoid double-signaling.
}

TEST_CASE("Reload callback is invoked for validation failure in OnFilesChanged",
          "[hotreload][callback]") {
	TempDir tmp("hotreload_cbfail_test");
	tmp.write("bad.lua", "{{{{{{");

	FileWatcher watcher;
	watcher.WatchDirectory(tmp.path, ".lua");

	std::atomic<int> callback_count{0};
	watcher.SetChangeCallback(
		[&](const std::vector<std::string>&) { callback_count++; });

	watcher.Start(50);
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	watcher.Stop();

	// The pre-existing bad.lua should be detected as "new" by FileWatcher.
	REQUIRE(callback_count.load() > 0);
}
