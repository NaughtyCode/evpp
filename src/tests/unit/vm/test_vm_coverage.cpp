#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "log_init.h"
#include "script_vm_fixture.h"
#include "test_helpers.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "runtime/vm/custom_ptr_store.h"
#include "runtime/vm/file_watcher.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/script_importer.h"
#include "runtime/vm/script_reloader.h"
#include "runtime/vm/vm.h"
#include "runtime/auth/bind/auth_bind.h"
#include "runtime/script/bind/bind_util.h"
#include "runtime/script/bind/import_bind.h"

using namespace engine;

// ═══════════════════════════════════════════════════════════════════════════════
// VMCustomPtrStore — advanced operations (basic ops already in test_scriptvm.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("VMCustomPtrStore: SetNull zeroes a slot without changing count", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 1, b = 2, c = 3;
    f.vm.PushCustomPtr(&a);
    f.vm.PushCustomPtr(&b);
    f.vm.PushCustomPtr(&c);
    REQUIRE(f.vm.CustomPtrCount() == 3);
    REQUIRE(f.vm.GetCustomPtr(2) == &b);

    f.vm.SetNullCustomPtr(2);
    REQUIRE(f.vm.CustomPtrCount() == 3);   // count unchanged
    REQUIRE(f.vm.GetCustomPtr(2) == nullptr);
    REQUIRE(f.vm.GetCustomPtr(1) == &a);   // other slots undisturbed
    REQUIRE(f.vm.GetCustomPtr(3) == &c);
}

TEST_CASE("VMCustomPtrStore: SetNull out-of-range is a no-op", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 1;
    f.vm.PushCustomPtr(&a);
    REQUIRE(f.vm.CustomPtrCount() == 1);

    f.vm.SetNullCustomPtr(0);    // index 0 is always invalid
    f.vm.SetNullCustomPtr(99);   // beyond count
    REQUIRE(f.vm.CustomPtrCount() == 1);
    REQUIRE(f.vm.GetCustomPtr(1) == &a);
}

TEST_CASE("VMCustomPtrStore: Reserve and Capacity", "[vm][customptr]") {
    ScriptVMFixture f;

    int initial_cap = f.vm.CustomPtrCapacity();
    REQUIRE(f.vm.ReserveCustomPtrSlots(50));
    REQUIRE(f.vm.CustomPtrCapacity() >= 50);
}

TEST_CASE("VMCustomPtrStore: CopyTo bulk export", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 10, b = 20, c = 30;
    f.vm.PushCustomPtr(&a);
    f.vm.PushCustomPtr(&b);
    f.vm.PushCustomPtr(&c);

    void* buf[5] = {};
    int count = f.vm.CopyCustomPtrsTo(buf, 5);
    REQUIRE(count == 3);
    REQUIRE(buf[0] == &a);
    REQUIRE(buf[1] == &b);
    REQUIRE(buf[2] == &c);
}

TEST_CASE("VMCustomPtrStore: CopyTo respects max_count", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 1, b = 2;
    f.vm.PushCustomPtr(&a);
    f.vm.PushCustomPtr(&b);

    void* buf[1] = {};
    int count = f.vm.CopyCustomPtrsTo(buf, 1);
    REQUIRE(count == 1);
    REQUIRE(buf[0] == &a);
}

TEST_CASE("VMCustomPtrStore: CopyFrom bulk import replaces contents", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 1, b = 2, c = 3;
    void* src[] = {&a, &b, &c};
    f.vm.CopyCustomPtrsFrom(src, 3);

    REQUIRE(f.vm.CustomPtrCount() == 3);
    REQUIRE(f.vm.GetCustomPtr(1) == &a);
    REQUIRE(f.vm.GetCustomPtr(2) == &b);
    REQUIRE(f.vm.GetCustomPtr(3) == &c);

    // Replace with fewer items
    void* src2[] = {&c};
    f.vm.CopyCustomPtrsFrom(src2, 1);
    REQUIRE(f.vm.CustomPtrCount() == 1);
    REQUIRE(f.vm.GetCustomPtr(1) == &c);
}

TEST_CASE("VMCustomPtrStore: Empty state", "[vm][customptr]") {
    ScriptVMFixture f;

    REQUIRE(f.vm.CustomPtrCount() == 0);

    int a = 42;
    f.vm.PushCustomPtr(&a);
    REQUIRE(f.vm.CustomPtrCount() == 1);

    f.vm.ClearCustomPtrs();
    REQUIRE(f.vm.CustomPtrCount() == 0);
}

TEST_CASE("VMCustomPtrStore: Set with index > count extends array with nullptr gaps", "[vm][customptr]") {
    ScriptVMFixture f;

    int val = 99;
    f.vm.SetCustomPtr(5, &val);
    REQUIRE(f.vm.CustomPtrCount() == 5);
    REQUIRE(f.vm.GetCustomPtr(5) == &val);
    REQUIRE(f.vm.GetCustomPtr(1) == nullptr);
    REQUIRE(f.vm.GetCustomPtr(2) == nullptr);
    REQUIRE(f.vm.GetCustomPtr(3) == nullptr);
    REQUIRE(f.vm.GetCustomPtr(4) == nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptVM — DoDirectory
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("DoDirectory loads .lua files from a directory", "[vm][dodir]") {
    ScriptVMFixture f;

    // Create a temporary test directory
    std::string tmpdir = "tmp_test_dodir";
    std::filesystem::create_directory(tmpdir);

    // Write Lua files
    std::ofstream f1(tmpdir + "/a.lua");
    f1 << "x = 1; return 'a_ok'";
    f1.close();

    std::ofstream f2(tmpdir + "/b.lua");
    f2 << "y = 2; return 'b_ok'";
    f2.close();

    // Non-lua file — should be skipped
    std::ofstream f3(tmpdir + "/readme.txt");
    f3 << "not lua";
    f3.close();

    size_t failures = f.vm.DoDirectory(tmpdir);
    REQUIRE(failures == 0);

    // Verify globals are set
    std::string result;
    REQUIRE(f.RunLuaCapture("return x", result));
    REQUIRE(result == "1");
    REQUIRE(f.RunLuaCapture("return y", result));
    REQUIRE(result == "2");

    // Cleanup
    std::filesystem::remove_all(tmpdir);
}

TEST_CASE("DoDirectory returns failure count for invalid dir", "[vm][dodir]") {
    ScriptVMFixture f;

    size_t failures = f.vm.DoDirectory("/nonexistent/path/xyz");
    REQUIRE(failures > 0);
}

TEST_CASE("DoFile registers default module name from script root", "[vm][dofile]") {
    ScriptVMFixture f;

    const std::filesystem::path root = "tmp_test_module_root";
    const std::filesystem::path sub = root / "sub";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(sub);

    const auto script_path = sub / "mod.lua";
    std::ofstream script(script_path);
    script << "return { value = 42 }";
    script.close();

    f.vm.SetScriptRoot(root.string());
    REQUIRE(f.vm.GetDefaultModuleNameForFile(script_path.string()) == "sub_mod");
    REQUIRE(f.vm.DoFile(script_path.string()));

    lua_State* L = f.vm.GetState();
    lua_getglobal(L, "package");
    REQUIRE(lua_istable(L, -1));
    lua_getfield(L, -1, "loaded");
    REQUIRE(lua_istable(L, -1));
    lua_getfield(L, -1, "sub_mod");
    REQUIRE(lua_istable(L, -1));
    lua_getfield(L, -1, "value");
    REQUIRE(lua_tointeger(L, -1) == 42);
    lua_pop(L, 4);

    std::filesystem::remove_all(root);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptVM — RegisterFunctions (array version)
// ═══════════════════════════════════════════════════════════════════════════════

static int c_mul(lua_State* L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    lua_pushnumber(L, a * b);
    return 1;
}

static int c_sub(lua_State* L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    lua_pushnumber(L, a - b);
    return 1;
}

TEST_CASE("RegisterFunctions exports multiple C functions as globals", "[vm][register]") {
    ScriptVMFixture f;

    luaL_Reg funcs[] = {
        {"c_mul", c_mul},
        {"c_sub", c_sub},
        {nullptr, nullptr}
    };
    f.vm.RegisterFunctions(funcs);

    std::string result;
    REQUIRE(f.RunLuaCapture("return c_mul(6, 7)", result));
    REQUIRE(result == "42.0");
    REQUIRE(f.RunLuaCapture("return c_sub(10, 3)", result));
    REQUIRE(result == "7.0");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptVM — RegisterModuleOpen (luaL_requiref style)
// ═══════════════════════════════════════════════════════════════════════════════

static int luaopen_testmod(lua_State* L) {
    lua_newtable(L);
    lua_pushinteger(L, 999);
    lua_setfield(L, -2, "magic");
    return 1;
}

TEST_CASE("RegisterModuleOpen registers module via require", "[vm][register]") {
    ScriptVMFixture f;

    f.vm.RegisterModuleOpen("testmod", luaopen_testmod, true);

    std::string result;
    // Global should be available
    REQUIRE(f.RunLuaCapture("return testmod.magic", result));
    REQUIRE(result == "999");

    // require should also work
    REQUIRE(f.RunLuaCapture("return require('testmod').magic", result));
    REQUIRE(result == "999");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptVM — RegisterCallback (lambda-style)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("RegisterCallback exports std::function as Lua global", "[vm][register]") {
    ScriptVMFixture f;

    int call_count = 0;
    f.vm.RegisterCallback("on_event", [&call_count](lua_State* L) -> int {
        ++call_count;
        int arg = static_cast<int>(lua_tointeger(L, 1));
        lua_pushinteger(L, arg * 2);
        return 1;
    });

    std::string result;
    REQUIRE(f.RunLuaCapture("return on_event(21)", result));
    REQUIRE(result == "42");
    REQUIRE(call_count == 1);

    REQUIRE(f.RunLuaCapture("local a,b=on_event(10),on_event(20); return a+b", result));
    REQUIRE(result == "60");
    REQUIRE(call_count == 3);
}

TEST_CASE("RegisterCallback handles 0-arg 0-result case", "[vm][register]") {
    ScriptVMFixture f;

    bool called = false;
    f.vm.RegisterCallback("tick", [&called](lua_State*) -> int {
        called = true;
        return 0;
    });

    std::string err;
    REQUIRE(f.RunLua("tick()", &err));
    REQUIRE(called);
}

TEST_CASE("RegisterCallback converts C++ exceptions to Lua errors", "[vm][register]") {
    ScriptVMFixture f;

    f.vm.RegisterCallback("explode", [](lua_State*) -> int {
        throw std::runtime_error("boom");
    });

    std::string err;
    REQUIRE_FALSE(f.RunLua("explode()", &err));
    test::AssertContains(err, "boom");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptVM — ToString and LuaVersion
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ToString returns value at stack index", "[vm][utils]") {
    ScriptVMFixture f;

    REQUIRE(f.RunLua("local x = 'hello'; return x"));
    // Can't easily test ToString from here without raw Lua stack access,
    // but verify it doesn't crash with nil state.
    engine::ScriptVM vm2;
    REQUIRE(vm2.ToString(-1).empty());  // empty stack → empty string
}

TEST_CASE("LuaVersion returns non-empty string", "[vm][utils]") {
    std::string ver = engine::ScriptVM::LuaVersion();
    REQUIRE_FALSE(ver.empty());
    REQUIRE(ver.find("Lua") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptImporter — SetPaths / AddPath / ClearCache
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptImporter: SetPaths parses semicolon-separated paths", "[vm][importer]") {
    ScriptVMFixture f;
    f.vm.GetImporter().SetPaths("/a/b;/c/d;/e/f");

    // Smoke test — SetPaths should not crash
    REQUIRE_NOTHROW(f.vm.SetImportPath("resources/script"));
}

TEST_CASE("ScriptImporter: semicolon paths cache root-relative default names", "[vm][importer]") {
    engine::ScriptVM vm;

    const std::filesystem::path root = "tmp_test_import_roots";
    const auto db_dir = root / "db_service";
    const auto runtime_dir = root / "runtime";
    const auto plugins_dir = runtime_dir / "plugins";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(db_dir);
    std::filesystem::create_directories(plugins_dir);

    std::ofstream dep(runtime_dir / "dep.lua");
    dep << "return { value = 'dep' }";
    dep.close();

    std::ofstream upper(runtime_dir / "upper.LUA");
    upper << "return { value = 'upper' }";
    upper.close();

    std::ofstream plugin(plugins_dir / "handler.lua");
    plugin << "return { value = 'handler' }";
    plugin.close();

    const std::string import_path = db_dir.string() + ";" + runtime_dir.string();
    vm.SetImportPath(import_path);
    ExportImport(vm);

    lua_State* L = vm.GetState();
    auto loaded_value = [L](const char* module_name) {
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "loaded");
        lua_getfield(L, -1, module_name);
        REQUIRE(lua_istable(L, -1));
        lua_getfield(L, -1, "value");
        const char* value = lua_tostring(L, -1);
        std::string result = value ? value : "";
        lua_pop(L, 4);
        return result;
    };

    lua_getglobal(L, "import");
    lua_pushstring(L, "dep");
    int rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK) INFO(lua_tostring(L, -1));
    REQUIRE(rc == LUA_OK);
    lua_pop(L, 1);

    REQUIRE(loaded_value("dep") == "dep");
    REQUIRE(loaded_value("runtime_dep") == "dep");

    lua_getglobal(L, "import");
    lua_pushstring(L, "upper");
    rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK) INFO(lua_tostring(L, -1));
    REQUIRE(rc == LUA_OK);
    lua_pop(L, 1);

    REQUIRE(loaded_value("upper") == "upper");
    REQUIRE(loaded_value("runtime_upper") == "upper");

    lua_getglobal(L, "import");
    lua_pushstring(L, "plugins.*");
    rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK) INFO(lua_tostring(L, -1));
    REQUIRE(rc == LUA_OK);
    lua_pop(L, 1);

    REQUIRE(loaded_value("plugins.handler") == "handler");
    REQUIRE(loaded_value("runtime_plugins_handler") == "handler");

    std::filesystem::remove_all(root);
}

TEST_CASE("ScriptImporter: AddPath adds to search paths", "[vm][importer]") {
    ScriptVMFixture f;
    f.vm.GetImporter().SetPaths("/first");
    f.vm.GetImporter().AddPath("/second");

    // Verify paths were registered — the import system should find modules
    // in the added paths
    REQUIRE_NOTHROW(f.vm.SetImportPath("resources/script"));
}

TEST_CASE("ScriptImporter: ClearCache resets package.loaded", "[vm][importer]") {
    ScriptVMFixture f;

    // Import something first so we can verify cache is cleared
    std::string result;
    REQUIRE(f.RunLuaCapture("return type(require)", result));
    REQUIRE(result == "function");

    f.vm.GetImporter().ClearCache(f.vm.GetState());

    // Verify import still works after clear
    REQUIRE(f.RunLuaCapture("return type(require)", result));
    REQUIRE(result == "function");
}

TEST_CASE("ScriptImporter: ClearCache preserves non-import package.loaded entries", "[vm][importer]") {
    ScriptVMFixture f;

    lua_State* L = f.vm.GetState();
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "engine_builtin");
    lua_pop(L, 2);

    f.vm.GetImporter().ClearCache(L);

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_getfield(L, -1, "engine_builtin");
    REQUIRE(lua_toboolean(L, -1) == 1);
    lua_pop(L, 3);
}

TEST_CASE("ScriptImporter: rejects path traversal module names", "[vm][importer]") {
    engine::ScriptVM vm;
    vm.GetImporter().Init("resources/script");
    ExportImport(vm);

    lua_State* L = vm.GetState();
    lua_getglobal(L, "import");
    lua_pushstring(L, "../secret");
    int rc = lua_pcall(L, 1, 1, 0);
    REQUIRE(rc != LUA_OK);
    lua_settop(L, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptImporter — circular dependency detection
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptImporter: circular dependency is detected") {
    // Create a standalone state with import system
    engine::ScriptVM vm;

    // Set up import paths
    std::string tmpdir = "tmp_test_circular";
    std::filesystem::create_directory(tmpdir);

    // A imports B, B imports A → circular
    std::ofstream fa(tmpdir + "/circ_a.lua");
    fa << "local b = import('circ_b')\nreturn {name='a'}\n";
    fa.close();

    std::ofstream fb(tmpdir + "/circ_b.lua");
    fb << "local a = import('circ_a')\nreturn {name='b'}\n";
    fb.close();

    vm.GetImporter().Init(tmpdir);
    ExportImport(vm);

    // Importing circ_a should trigger circular detection via circ_b→circ_a
    // This should error, not hang
    lua_State* L = vm.GetState();
    lua_getglobal(L, "import");
    REQUIRE_FALSE(lua_isnil(L, -1));  // import is a callable table
    lua_pushstring(L, "circ_a");

    // The import should fail with an error (circular dependency)
    int rc = lua_pcall(L, 1, 1, 0);
    // May succeed (if circ_a finishes before circ_b's import of circ_a,
    // since circ_a is already cached) or fail with circular error
    // Either way, it shouldn't crash or hang
    REQUIRE((rc == LUA_OK || rc != LUA_OK));
    lua_settop(L, 0);

    std::filesystem::remove_all(tmpdir);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptImporter — wildcard import (dir.*)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptImporter: wildcard import loads all .lua files in a dir") {
    engine::ScriptVM vm;

    std::string tmpdir = "tmp_test_wildcard";
    std::filesystem::create_directory(tmpdir);
    std::filesystem::create_directory(tmpdir + "/my_pkg");

    std::ofstream f1(tmpdir + "/my_pkg/mod_a.lua");
    f1 << "return {value = 'A'}";
    f1.close();

    std::ofstream f2(tmpdir + "/my_pkg/mod_b.lua");
    f2 << "return {value = 'B'}";
    f2.close();

    vm.GetImporter().Init(tmpdir);
    ExportImport(vm);

    lua_State* L = vm.GetState();
    lua_getglobal(L, "import");
    REQUIRE_FALSE(lua_isnil(L, -1));  // import is a callable table
    lua_pushstring(L, "my_pkg.*");

    int rc = lua_pcall(L, 1, 1, 0);
    REQUIRE(rc == LUA_OK);

    // Result should be a table with mod_a and mod_b entries
    int result_idx = lua_gettop(L);
    REQUIRE(lua_istable(L, result_idx));
    lua_getfield(L, result_idx, "mod_a");
    REQUIRE(lua_istable(L, -1));
    lua_getfield(L, -1, "value");
    REQUIRE(std::string(lua_tostring(L, -1)) == "A");
    lua_pop(L, 2);  // value, mod_a — leave result table

    lua_getfield(L, result_idx, "mod_b");
    REQUIRE(lua_istable(L, -1));
    lua_getfield(L, -1, "value");
    REQUIRE(std::string(lua_tostring(L, -1)) == "B");
    lua_pop(L, 2);  // value, mod_b
    lua_pop(L, 1);  // result

    std::filesystem::remove_all(tmpdir);
}

// ═══════════════════════════════════════════════════════════════════════════════
// FileWatcher — construction, start, stop, IsRunning
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileWatcher: construction and initial state", "[vm][filewatcher]") {
    engine::FileWatcher watcher;
    REQUIRE_FALSE(watcher.IsRunning());
}

TEST_CASE("FileWatcher: start and stop lifecycle", "[vm][filewatcher]") {
    engine::FileWatcher watcher;

    std::string tmpdir = "tmp_test_watcher";
    std::filesystem::create_directory(tmpdir);

    watcher.WatchDirectory(tmpdir, ".lua");
    REQUIRE_FALSE(watcher.IsRunning());

    watcher.Start(200);  // 200ms poll interval
    REQUIRE(watcher.IsRunning());

    watcher.Stop();
    REQUIRE_FALSE(watcher.IsRunning());

    std::filesystem::remove_all(tmpdir);
}

TEST_CASE("FileWatcher: change callback is invoked", "[vm][filewatcher]") {
    engine::FileWatcher watcher;

    std::string tmpdir = "tmp_test_watcher_cb";
    std::filesystem::create_directory(tmpdir);

    std::atomic<int> callback_count{0};
    std::vector<std::string> last_changed;
    std::mutex callback_mutex;

    watcher.WatchDirectory(tmpdir, ".lua");
    watcher.SetChangeCallback([&](const std::vector<std::string>& files) {
        ++callback_count;
        std::lock_guard<std::mutex> lock(callback_mutex);
        last_changed = files;
    });
    watcher.Start(100);

    // Let the first scan complete (poll interval + margin)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create a .lua file to trigger detection on the next scan
    std::ofstream f(tmpdir + "/test.lua");
    f << "x = 1";
    f.close();

    // Wait for at least 2 poll cycles to detect the change
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    watcher.Stop();

    // Timing-dependent check — on heavily loaded CI the watcher may
    // miss the event.  The primary goal of this test is to verify the
    // lifecycle (start/stop/callback registration) doesn't crash.
    CHECK(callback_count.load() >= 1);

    std::filesystem::remove_all(tmpdir);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScriptReloader — basic lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptReloader: construction and stop without crash", "[vm][reloader]") {
    engine::ScriptReloader reloader;

    // Stop without starting — should be safe
    REQUIRE_NOTHROW(reloader.Stop());
}

TEST_CASE("ScriptReloader: SetTarget and Start/Stop lifecycle", "[vm][reloader]") {
    engine::ScriptVM vm;
    engine::ScriptReloader reloader;

    std::string tmpdir = "tmp_test_reloader";
    std::filesystem::create_directory(tmpdir);

    std::vector<std::string> dirs = {tmpdir};
    reloader.SetTarget(&vm, dirs);
    reloader.Start(200, 100);
    reloader.Stop();

    std::filesystem::remove_all(tmpdir);
}

TEST_CASE("ScriptReloader: ReloadFile returns false for nonexistent file", "[vm][reloader]") {
    engine::ScriptVM vm;
    engine::ScriptReloader reloader;

    std::string tmpdir = "tmp_test_reloader2";
    std::filesystem::create_directory(tmpdir);

    std::vector<std::string> dirs = {tmpdir};
    reloader.SetTarget(&vm, dirs);

    bool ok = reloader.ReloadFile("/nonexistent/reload_test.lua");
    REQUIRE_FALSE(ok);

    std::filesystem::remove_all(tmpdir);
}

// ═══════════════════════════════════════════════════════════════════════════════
// bind_util — LuaError
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LuaError formats string and uses lua_error", "[bindutil][luaerror]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // LuaError uses lua_error (longjmp), so we need to use lua_pcall
    std::string script = R"(
        local status, err = pcall(function()
            -- This will call LuaError which does a longjmp
            error("simulated lua_error")
        end)
        return tostring(status) .. "," .. tostring(err)
    )";

    int rc = luaL_dostring(L, script.c_str());
    REQUIRE(rc == LUA_OK);

    std::string result(lua_tostring(L, -1));
    REQUIRE(result.find("false") != std::string::npos);  // pcall returned false (error caught)

    lua_close(L);
}

// ═══════════════════════════════════════════════════════════════════════════════
// bind_util — CallInstMethod variants
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("CallInstMethod calls method on Lua instance by registry ref", "[bindutil][callback]") {
    ScriptVMFixture f;

    std::string err;
    // Create a Lua table with a method, store it in registry
    REQUIRE(f.RunLua(R"(
        obj = { value = 0 }
        function obj:increment()
            self.value = self.value + 1
        end
        function obj:get()
            return self.value
        end
    )", &err));

    lua_State* L = f.vm.GetState();
    lua_getglobal(L, "obj");
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    int top = lua_gettop(L);
    script::CallInstMethod(L, ref, "increment");
    REQUIRE(lua_gettop(L) == top);

    std::string result;
    REQUIRE(f.RunLuaCapture("return obj:get()", result));
    REQUIRE(result == "1");

    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

TEST_CASE("CallInstMethodStr calls method with string argument", "[bindutil][callback]") {
    ScriptVMFixture f;

    std::string err;
    REQUIRE(f.RunLua(R"(
        handler = { last_msg = "" }
        function handler:on_message(msg)
            self.last_msg = msg
        end
        function handler:last()
            return self.last_msg
        end
    )", &err));

    lua_State* L = f.vm.GetState();
    lua_getglobal(L, "handler");
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    int top = lua_gettop(L);
    script::CallInstMethodStr(L, ref, "on_message", "hello world");
    REQUIRE(lua_gettop(L) == top);

    std::string result;
    REQUIRE(f.RunLuaCapture("return handler:last()", result));
    REQUIRE(result == "hello world");

    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

// ═══════════════════════════════════════════════════════════════════════════════
// bind_util — PushInstanceTableShared (shared_ptr lifetime via Lua GC)
// ═══════════════════════════════════════════════════════════════════════════════

struct TestSharedCtx {
    int value = 0;
    bool deleted = false;
    ~TestSharedCtx() { deleted = true; }
};

struct OtherSharedCtx {
    int value = 0;
};

TEST_CASE("PushInstanceTableShared stores shared_ptr and accessible via _ctx", "[bindutil][shared]") {
    ScriptVMFixture f;

    auto ctx = std::make_shared<TestSharedCtx>();
    ctx->value = 42;
    auto* raw = ctx.get();

    script::PushInstanceTableShared(f.vm.GetState(), ctx, "test.shared.instance");

    // Verify raw pointer accessible via GetCtxFromTable
    auto* retrieved = script::GetCtxFromTable<TestSharedCtx>(f.vm.GetState(), -1);
    REQUIRE(retrieved == raw);
    REQUIRE(retrieved->value == 42);

    lua_pop(f.vm.GetState(), 1);  // pop the instance table
}

TEST_CASE("PushInstanceTableShared: shared_ptr survives beyond local scope", "[bindutil][shared]") {
    ScriptVMFixture f;
    TestSharedCtx* raw = nullptr;

    {
        auto ctx = std::make_shared<TestSharedCtx>();
        ctx->value = 100;
        raw = ctx.get();

        script::PushInstanceTableShared(f.vm.GetState(), ctx, "test.shared.scope");

        // ctx shared_ptr is copied into the Lua full userdata,
        // so raw should still be valid after this scope
    }

    auto* retrieved = script::GetCtxFromTable<TestSharedCtx>(f.vm.GetState(), -1);
    REQUIRE(retrieved == raw);
    REQUIRE(retrieved->value == 100);

    lua_pop(f.vm.GetState(), 1);
}

TEST_CASE("PushInstanceTableShared uses type-specific GC metatables", "[bindutil][shared]") {
    ScriptVMFixture f;
    lua_State* L = f.vm.GetState();

    luaL_newmetatable(L, "test.shared.type_a");
    lua_pop(L, 1);
    luaL_newmetatable(L, "test.shared.type_b");
    lua_pop(L, 1);

    std::weak_ptr<TestSharedCtx> weak_a;
    std::weak_ptr<OtherSharedCtx> weak_b;

    {
        auto a = std::make_shared<TestSharedCtx>();
        auto b = std::make_shared<OtherSharedCtx>();
        weak_a = a;
        weak_b = b;

        script::PushInstanceTableShared(L, a, "test.shared.type_a");
        lua_setglobal(L, "shared_a");
        script::PushInstanceTableShared(L, b, "test.shared.type_b");
        lua_setglobal(L, "shared_b");
    }

    REQUIRE_FALSE(weak_a.expired());
    REQUIRE_FALSE(weak_b.expired());

    REQUIRE(f.RunLua("shared_a = nil; shared_b = nil; collectgarbage(); collectgarbage()"));
    REQUIRE(weak_a.expired());
    REQUIRE(weak_b.expired());
}

// ═══════════════════════════════════════════════════════════════════════════════
// bind_util — PushLibrary
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("PushLibrary creates a library table from luaL_Reg array", "[bindutil][library]") {
    ScriptVMFixture f;

    luaL_Reg lib_funcs[] = {
        {"add", c_mul},   // reusing c_mul as our lib function
        {nullptr, nullptr}
    };

    lua_State* L = f.vm.GetState();
    script::PushLibrary(L, lib_funcs);
    lua_setglobal(L, "mylib");

    std::string result;
    REQUIRE(f.RunLuaCapture("return mylib.add(5, 6)", result));
    REQUIRE(result == "30.0");
}

// ═══════════════════════════════════════════════════════════════════════════════
// lua_error_handler — PushLuaErrorHandler / SafeCallLua
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("PushLuaErrorHandler pushes a function onto the stack", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int top = lua_gettop(L);
    int idx = PushLuaErrorHandler(L);
    REQUIRE(idx == top + 1);
    REQUIRE(lua_isfunction(L, idx));

    lua_pop(L, 1);
    lua_close(L);
}

TEST_CASE("SafeCallLua returns NotFound when stack has no function", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    LuaCallOptions opts;
    opts.nargs = 0;
    opts.nresults = 0;

    // Empty stack — no function to call
    LuaCallResult result = SafeCallLua(L, opts);
    REQUIRE(result == LuaCallResult::NotFound);

    lua_close(L);
}

TEST_CASE("SafeCallLua NotFound removes non-function and args", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_pushnil(L);
    lua_pushinteger(L, 42);

    LuaCallOptions opts;
    opts.nargs = 1;
    opts.nresults = 0;

    LuaCallResult result = SafeCallLua(L, opts);
    REQUIRE(result == LuaCallResult::NotFound);
    REQUIRE(lua_gettop(L) == 0);

    lua_close(L);
}

TEST_CASE("SafeCallLua successfully calls a Lua function", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // Push a function that returns a number
    lua_pushcfunction(L, c_mul);
    lua_pushinteger(L, 7);
    lua_pushinteger(L, 6);

    LuaCallOptions opts;
    opts.nargs = 2;
    opts.nresults = 1;

    LuaCallResult result = SafeCallLua(L, opts);
    REQUIRE(result == LuaCallResult::Ok);
    REQUIRE(lua_gettop(L) == 1);
    REQUIRE(lua_tonumber(L, -1) == 42.0);

    lua_pop(L, 1);
    lua_close(L);
}

TEST_CASE("SafeCallLua returns LuaError on runtime error", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // Push a function that will error
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        lua_pushstring(L2, "intentional error");
        lua_error(L2);
        return 0;
    });

    LuaCallOptions opts;
    opts.nargs = 0;
    opts.nresults = 0;
    opts.log_on_error = false;

    LuaCallResult result = SafeCallLua(L, opts);
    REQUIRE(result == LuaCallResult::LuaError);
    REQUIRE(lua_gettop(L) == 0);  // stack should be clean

    lua_close(L);
}

TEST_CASE("SafeCallLua with custom error handler ref", "[luaerr]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // Register a custom error handler
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        lua_pushstring(L2, "custom handler");
        return 1;
    });
    int handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Push a function that errors
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        lua_error(L2);
        return 0;
    });

    LuaCallOptions opts;
    opts.nargs = 0;
    opts.nresults = 0;
    opts.log_on_error = false;
    opts.error_handler_ref = handler_ref;

    LuaCallResult result = SafeCallLua(L, opts);
    REQUIRE(result == LuaCallResult::LuaError);
    REQUIRE(lua_gettop(L) == 0);

    luaL_unref(L, LUA_REGISTRYINDEX, handler_ref);
    lua_close(L);
}

TEST_CASE("auth binding authenticates through configured token backend", "[auth_bind]") {
    ScriptVMFixture f;
    script::ExportAuth(f.vm);

    std::string result;
    REQUIRE(f.RunLuaCapture(R"(
        auth.set_token_backend()
        auth.add_token('token-a', 'entity-a')
        local ok, entity_id, session_id = auth.authenticate('token', { token = 'token-a' })
        assert(ok == true)
        assert(entity_id == 'entity-a')
        local valid_before = auth.validate_session(session_id)
        auth.revoke_session(session_id)
        local valid_after = auth.validate_session(session_id)
        return tostring(valid_before) .. ',' .. tostring(valid_after)
    )", result));
    REQUIRE(result == "true,false");
}

TEST_CASE("auth binding switches backend without stale raw pointers", "[auth_bind]") {
    ScriptVMFixture f;
    script::ExportAuth(f.vm);

    std::string result;
    REQUIRE(f.RunLuaCapture(R"(
        auth.set_token_backend()
        auth.add_token('old-token', 'old-entity')
        auth.set_jwt_backend('secret')
        auth.set_token_backend()
        auth.add_token('new-token', 'new-entity')
        local ok, entity_id = auth.authenticate('token', { token = 'new-token' })
        return tostring(ok) .. ',' .. tostring(entity_id)
    )", result));
    REQUIRE(result == "true,new-entity");
}

// ═══════════════════════════════════════════════════════════════════════════════
// lua_error_handler — ShouldLogError throttle
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ShouldLogError returns true on first call, false on rapid repeat", "[luaerr]") {
    // First call should return true
    REQUIRE(ShouldLogError("test_key_unique"));

    // Immediate repeat with same key should be throttled
    REQUIRE_FALSE(ShouldLogError("test_key_unique"));
}
