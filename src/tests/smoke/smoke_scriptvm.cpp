#include <catch2/catch_test_macros.hpp>
#include "script_vm_fixture.h"

// Smoke test: ScriptVM create, execute a simple script, verify result.

TEST_CASE("ScriptVM create and execute", "[smoke][vm]") {
    ScriptVMFixture f;

    REQUIRE(f.vm.GetState() != nullptr);

    std::string result;
    REQUIRE(f.RunLuaCapture("return 42", result));
    REQUIRE(result == "42");

    REQUIRE(f.RunLuaCapture("return 'hello'", result));
    REQUIRE(result == "hello");

    REQUIRE(f.RunLuaCapture("return 1 + 2 * 3", result));
    REQUIRE(result == "7");

    // Error path
    std::string err;
    REQUIRE_FALSE(f.RunLua("this_is_not_valid_syntax(", &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("ScriptVM global set/get roundtrip", "[smoke][vm]") {
    ScriptVMFixture f;

    f.SetGlobalInt("answer", 42);
    std::string result;
    REQUIRE(f.RunLuaCapture("return answer", result));
    REQUIRE(result == "42");

    f.SetGlobalStr("greeting", "hello world");
    REQUIRE(f.RunLuaCapture("return greeting", result));
    REQUIRE(result == "hello world");
}
