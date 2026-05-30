#include <catch2/catch_test_macros.hpp>

#include "log_init.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "runtime/vm/sandbox.h"
#include "runtime/vm/vm.h"

namespace {

/** Create a standalone Lua state with sandboxed libraries, run a Lua
 *  expression that returns a boolean, and return the result. */
bool EvalBool(engine::LuaSandboxLevel level, const char* expr) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    engine::luaL_openlibs_sandboxed(L, level);
    int rc = luaL_dostring(L, expr);
    bool result = (rc == LUA_OK) && lua_toboolean(L, -1);
    lua_close(L);
    return result;
}


}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * Always-on libraries (all levels)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Always-on: base library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(print) == 'function'"));
        REQUIRE(EvalBool(level, "return type(error) == 'function'"));
        REQUIRE(EvalBool(level, "return type(pcall)  == 'function'"));
    }
}

TEST_CASE("Always-on: table library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(table.insert) == 'function'"));
        REQUIRE(EvalBool(level, "return type(table.sort)   == 'function'"));
    }
}

TEST_CASE("Always-on: string library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(string.find)  == 'function'"));
        REQUIRE(EvalBool(level, "return type(string.gsub)  == 'function'"));
    }
}

TEST_CASE("Always-on: math library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(math.abs)   == 'function'"));
        REQUIRE(EvalBool(level, "return type(math.floor) == 'function'"));
    }
}

TEST_CASE("Always-on: utf8 library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(utf8.char) == 'function'"));
    }
}

TEST_CASE("Always-on: coroutine library", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(coroutine.create) == 'function'"));
    }
}

TEST_CASE("Always-on: package library (loadlib removed)", "[sandbox][all]") {
    for (auto level : {engine::LuaSandboxLevel::Strict,
                       engine::LuaSandboxLevel::Server,
                       engine::LuaSandboxLevel::Full}) {
        REQUIRE(EvalBool(level, "return type(package.searchpath) == 'function'"));
        REQUIRE(EvalBool(level, "return package.loadlib == nil"));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Strict level — no io, sanitized os, no debug
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Strict: io library is absent", "[sandbox][strict]") {
    REQUIRE(EvalBool(engine::LuaSandboxLevel::Strict, "return io == nil"));
}

TEST_CASE("Strict: os library has dangerous functions removed", "[sandbox][strict]") {
    auto level = engine::LuaSandboxLevel::Strict;
    REQUIRE(EvalBool(level, "return type(os.clock) == 'function'"));
    REQUIRE(EvalBool(level, "return os.execute == nil"));
    REQUIRE(EvalBool(level, "return os.exit    == nil"));
    REQUIRE(EvalBool(level, "return os.remove  == nil"));
    REQUIRE(EvalBool(level, "return os.rename  == nil"));
    REQUIRE(EvalBool(level, "return os.getenv  == nil"));
}

TEST_CASE("Strict: debug library is absent", "[sandbox][strict]") {
    REQUIRE(EvalBool(engine::LuaSandboxLevel::Strict, "return debug == nil"));
}

TEST_CASE("Strict: base file loaders are absent", "[sandbox][strict]") {
    auto level = engine::LuaSandboxLevel::Strict;
    REQUIRE(EvalBool(level, "return dofile == nil"));
    REQUIRE(EvalBool(level, "return loadfile == nil"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Server level — io + os allowed, no debug
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Server: io library is present", "[sandbox][server]") {
    REQUIRE(EvalBool(engine::LuaSandboxLevel::Server, "return type(io.write) == 'function'"));
}

TEST_CASE("Server: os library has dangerous functions", "[sandbox][server]") {
    auto level = engine::LuaSandboxLevel::Server;
    REQUIRE(EvalBool(level, "return type(os.execute) == 'function'"));
    REQUIRE(EvalBool(level, "return type(os.exit)    == 'function'"));
    REQUIRE(EvalBool(level, "return type(os.getenv)  == 'function'"));
}

TEST_CASE("Server: debug library is absent", "[sandbox][server]") {
    REQUIRE(EvalBool(engine::LuaSandboxLevel::Server, "return debug == nil"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Full level — all libraries loaded, only package.loadlib removed
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Full: all libraries present", "[sandbox][full]") {
    auto level = engine::LuaSandboxLevel::Full;
    REQUIRE(EvalBool(level, "return type(io.write)      == 'function'"));
    REQUIRE(EvalBool(level, "return type(os.execute)    == 'function'"));
    REQUIRE(EvalBool(level, "return type(debug.getinfo) == 'function'"));
}

TEST_CASE("Full: package.loadlib still removed", "[sandbox][full]") {
    REQUIRE(EvalBool(engine::LuaSandboxLevel::Full, "return package.loadlib == nil"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ScriptVM integration — sandbox level passed through constructor
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("ScriptVM default constructs with Full sandbox", "[sandbox][integration]") {
    engine::ScriptVM vm;
    std::string error;
    /* debug library should be available in Full mode */
    bool ok = vm.DoString("return type(debug.getinfo) == 'function'", "test", &error);
    REQUIRE(ok);
}

TEST_CASE("ScriptVM Strict sandbox blocks io", "[sandbox][integration]") {
    engine::ScriptVM vm(engine::LuaSandboxLevel::Strict);
    std::string result;
    bool ok = vm.DoString("if io == nil then return 'blocked' else return 'available' end", "test", nullptr, &result);
    REQUIRE(ok);
    REQUIRE(result == "blocked");
}
