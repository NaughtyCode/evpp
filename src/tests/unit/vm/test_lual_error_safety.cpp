#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "log_init.h"
#include "runtime/script/bind_util.h"
#include "runtime/vm/vm.h"

namespace {

/** Counter for verifying destructor calls across scope boundaries. */
struct TrackedResource {
    static int alive_count;
    TrackedResource() { alive_count++; }
    ~TrackedResource() { alive_count--; }

    TrackedResource(const TrackedResource&) = delete;
    TrackedResource& operator=(const TrackedResource&) = delete;
};
int TrackedResource::alive_count = 0;

}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * LuaError helper function
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("LuaError formats and pushes error message", "[lual_error][helper]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_pushcfunction(
        L, [](lua_State* L2) -> int { return engine::script::LuaError(L2, "test error %d", 42); });
    int rc = lua_pcall(L, 0, 0, 0);
    REQUIRE(rc == LUA_ERRRUN);
    std::string msg(lua_tostring(L, -1));
    REQUIRE(msg == "test error 42");
    lua_close(L);
}

TEST_CASE("LuaError with no format args", "[lual_error][helper]") {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_pushcfunction(
        L, [](lua_State* L2) -> int { return engine::script::LuaError(L2, "simple message"); });
    int rc = lua_pcall(L, 0, 0, 0);
    REQUIRE(rc == LUA_ERRRUN);
    std::string msg(lua_tostring(L, -1));
    REQUIRE(msg == "simple message");
    lua_close(L);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scope-block pattern: RAII cleanup before error path
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("TrackedResource destructor runs on normal scope exit", "[lual_error][scope_block]") {
    TrackedResource::alive_count = 0;
    {
        auto res1 = std::make_unique<TrackedResource>();
        REQUIRE(TrackedResource::alive_count == 1);
        {
            auto res2 = std::make_unique<TrackedResource>();
            REQUIRE(TrackedResource::alive_count == 2);
        }
        REQUIRE(TrackedResource::alive_count == 1);
    }
    REQUIRE(TrackedResource::alive_count == 0);
}

TEST_CASE("Scope block protects RAII before LuaError", "[lual_error][scope_block]") {
    /* Simulate the pattern used in l_net_server_listen:
     * wrap RAII objects in a scope, extract raw pointer, then
     * call LuaError (longjmp) after scope exit. */
    TrackedResource::alive_count = 0;

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    TrackedResource* raw = nullptr;
    {
        auto guard = std::make_unique<TrackedResource>();
        REQUIRE(TrackedResource::alive_count == 1);
        raw = guard.release();  /* transfer ownership */
    }
    /* guard destroyed (it was released, so no dtor), alive_count still 1 */
    REQUIRE(TrackedResource::alive_count == 1);

    /* Now simulate: Lua longjmp error — raw resource would leak if not
     * cleaned up. But this pattern ensures the unique_ptr gave up
     * ownership BEFORE the scope exit, so the unique_ptr dtor is a
     * no-op. The raw pointer is used after the error check. */
    delete raw;
    REQUIRE(TrackedResource::alive_count == 0);

    lua_close(L);
}

TEST_CASE("Scope block destroys RAII on early error exit", "[lual_error][scope_block]") {
    /* Pattern: RAII objects in scope block, error detected inside block,
     * unique_ptr destructor runs at closing brace BEFORE any luaL_error.
     * This is the core pattern used in l_net_server_listen. */
    TrackedResource::alive_count = 0;

    bool error_occurred = false;
    TrackedResource* raw = nullptr;
    {
        auto res = std::make_unique<TrackedResource>();
        REQUIRE(TrackedResource::alive_count == 1);

        /* Simulated init failure — exit scope instead of calling luaL_error */
        if (/* Init() failed */ true) {
            error_occurred = true;
            /* res is destroyed here as scope exits — no leak */
        } else {
            raw = res.release();
        }
    }
    /* After scope: res destructor ran (or was released). Either way,
     * no RAII objects remain on stack — safe to call luaL_error. */
    REQUIRE(TrackedResource::alive_count == 0);
    REQUIRE(error_occurred == true);
    REQUIRE(raw == nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ScriptVM error path integration
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("ScriptVM survives luaL_error via pcall", "[lual_error][integration]") {
    engine::ScriptVM vm;
    std::string error;
    /* luaL_error triggers longjmp back to pcall — pcall catches it.
     * This verifies the VM is NOT corrupted after the longjmp. */
    bool ok = vm.DoString(R"lua(
        local ok, err = pcall(function()
            error("deliberate longjmp test")
        end)
        assert(ok == false)
        assert(err ~= nil)
    )lua", "test", &error);
    REQUIRE(ok);
    /* VM still usable after pcall catches the longjmp */
    std::string result;
    ok = vm.DoString("return 'still alive'", "test2", nullptr, &result);
    REQUIRE(ok);
    REQUIRE(result == "still alive");
}
