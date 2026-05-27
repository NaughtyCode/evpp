#include <catch2/catch_test_macros.hpp>
#include "script_vm_fixture.h"
#include "test_helpers.h"

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: construction & lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptVM constructs with valid lua_State", "[vm][lifecycle]") {
    ScriptVMFixture f;
    REQUIRE(f.vm.GetState() != nullptr);
}

TEST_CASE("ScriptVM move construction", "[vm][lifecycle]") {
    engine::ScriptVM a;
    auto* state_a = a.GetState();
    REQUIRE(state_a != nullptr);

    engine::ScriptVM b(std::move(a));
    REQUIRE(b.GetState() == state_a);
    REQUIRE(a.GetState() == nullptr);  // moved-from
}

TEST_CASE("ScriptVM move assignment", "[vm][lifecycle]") {
    engine::ScriptVM a;
    engine::ScriptVM b;
    auto* state_b = b.GetState();

    b = std::move(a);
    REQUIRE(b.GetState() != state_b);  // b's old state was destroyed, a's state moved in
    REQUIRE(a.GetState() == nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: DoString execution
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DoString executes valid Lua", "[vm][exec]") {
    ScriptVMFixture f;

    std::string result;
    REQUIRE(f.RunLuaCapture("return 42", result));
    REQUIRE(result == "42");

    REQUIRE(f.RunLuaCapture("return 'hello world'", result));
    REQUIRE(result == "hello world");

    REQUIRE(f.RunLuaCapture("return 1 + 2 * 3", result));
    REQUIRE(result == "7");

    REQUIRE(f.RunLuaCapture("return true", result));
    REQUIRE(result == "true");
}

TEST_CASE("DoString returns false and error on bad syntax", "[vm][exec]") {
    ScriptVMFixture f;

    std::string err;
    REQUIRE_FALSE(f.RunLua("syntax error !!!", &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("DoString chunk_name appears in error", "[vm][exec]") {
    ScriptVMFixture f;

    std::string err;
    f.vm.DoString("error('bad thing')", "my_chunk", &err);
    test::AssertContains(err, "bad thing");
}

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: RegisterFunction & RegisterModule
// ═══════════════════════════════════════════════════════════════════════════

static int c_add(lua_State* L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

TEST_CASE("RegisterFunction exports C function to Lua", "[vm][register]") {
    ScriptVMFixture f;
    f.vm.RegisterFunction("c_add", c_add);

    std::string result;
    REQUIRE(f.RunLuaCapture("return c_add(2, 3)", result));
    REQUIRE(result == "5.0");
}

TEST_CASE("RegisterModule creates table with functions", "[vm][register]") {
    ScriptVMFixture f;

    luaL_Reg funcs[] = {
        {"add", c_add},
        {nullptr, nullptr}
    };
    f.vm.RegisterModule("mymath", funcs);

    std::string result;
    REQUIRE(f.RunLuaCapture("return mymath.add(10, 20)", result));
    REQUIRE(result == "30.0");
}

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: SetGlobal
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SetGlobal int accessible from Lua", "[vm][globals]") {
    ScriptVMFixture f;
    f.SetGlobalInt("x", 100);

    std::string result;
    REQUIRE(f.RunLuaCapture("return x", result));
    REQUIRE(result == "100");

    REQUIRE(f.RunLuaCapture("return x + 1", result));
    REQUIRE(result == "101");
}

TEST_CASE("SetGlobal string accessible from Lua", "[vm][globals]") {
    ScriptVMFixture f;
    f.SetGlobalStr("name", "cloud");

    std::string result;
    REQUIRE(f.RunLuaCapture("return name", result));
    REQUIRE(result == "cloud");
}

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: CustomPtrStore
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CustomPtrStore push and get", "[vm][customptr]") {
    ScriptVMFixture f;

    int val = 42;
    int idx = f.vm.PushCustomPtr(&val);
    REQUIRE(idx > 0);

    void* ptr = f.vm.GetCustomPtr(idx);
    REQUIRE(ptr == &val);

    int* typed = f.vm.GetCustomPtrAs<int>(idx);
    REQUIRE(*typed == 42);
}

TEST_CASE("CustomPtrStore set and get", "[vm][customptr]") {
    ScriptVMFixture f;

    f.vm.ReserveCustomPtrSlots(5);
    double val = 3.14;
    f.vm.SetCustomPtr(3, &val);

    double* retrieved = f.vm.GetCustomPtrAs<double>(3);
    REQUIRE(retrieved == &val);
    REQUIRE(*retrieved == 3.14);
}

TEST_CASE("CustomPtrStore clear", "[vm][customptr]") {
    ScriptVMFixture f;

    int a = 1, b = 2, c = 3;
    f.vm.PushCustomPtr(&a);
    f.vm.PushCustomPtr(&b);
    f.vm.PushCustomPtr(&c);
    REQUIRE(f.vm.CustomPtrCount() == 3);

    f.vm.ClearCustomPtrs();
    REQUIRE(f.vm.CustomPtrCount() == 0);
}

TEST_CASE("CustomPtrStore find", "[vm][customptr]") {
    ScriptVMFixture f;

    int x = 10, y = 20;
    f.vm.PushCustomPtr(&x);
    f.vm.PushCustomPtr(&y);

    REQUIRE(f.vm.FindCustomPtr(&x) == 1);
    REQUIRE(f.vm.FindCustomPtr(&y) == 2);
    REQUIRE(f.vm.FindCustomPtr(nullptr) == -1);
    REQUIRE(f.vm.ContainsCustomPtr(&x));
    REQUIRE_FALSE(f.vm.ContainsCustomPtr(nullptr));
}

// ═══════════════════════════════════════════════════════════════════════════
// ScriptVM: ScriptImporter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ScriptImporter set path propagates", "[vm][importer]") {
    ScriptVMFixture f;
    REQUIRE_NOTHROW(f.vm.SetImportPath("resources/script"));
}
