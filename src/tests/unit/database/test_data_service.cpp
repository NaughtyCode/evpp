#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log_init.h"

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/data_service/db_service_main_bind.h"
#include "runtime/script/json_bind.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lua.h"
}

using namespace engine;

TEST_CASE("DatabaseService is unhealthy before initialization", "[database][data_service]") {
    DatabaseService::Instance().Shutdown();

    REQUIRE_FALSE(DatabaseService::Instance().IsRunning());
    REQUIRE_FALSE(DatabaseService::Instance().IsHealthy());
    REQUIRE(DatabaseService::Instance().GetThreadCount() == 0);
}

TEST_CASE("DBScriptVM frame callback keeps the Lua stack balanced", "[database][data_service]") {
    DBScriptVM vm;
    std::string error;

    REQUIRE(vm.DoString("function on_db_frame(info) return nil end", "=data_service_test", &error));
    auto* L = vm.GetState();
    const int baseline = lua_gettop(L);

    for (int i = 0; i < 8; ++i) {
        vm.CallFrameCallback(i + 1, 0.016);
        REQUIRE(lua_gettop(L) == baseline);
    }
}

TEST_CASE("DBScriptVM can host json bindings", "[database][data_service][json]") {
    DBScriptVM vm;
    script::ExportJson(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local value = json.decode('{\"name\":\"db\",\"items\":[1,null]}')\n"
        "return value.name .. '|' .. tostring(value.items[1]) .. '|' .. json.type(value.items[2])",
        "=data_service_json_test",
        &error,
        &result));

    REQUIRE(result == "db|1|null");
}

TEST_CASE("db_send_request rejects internal noop operation", "[database][data_service]") {
    ScriptVM vm;
    script::ExportDbService(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local ok, err = db_send_request({ operation = 'noop' })\n"
        "return tostring(ok) .. '|' .. tostring(err)",
        "=data_service_noop_test",
        &error,
        &result));

    REQUIRE(result == "false|db_send_request: noop is an internal operation");
}

TEST_CASE("db_send_request validates allow_empty_filter type", "[database][data_service]") {
    ScriptVM vm;
    script::ExportDbService(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local ok, err = db_send_request({ operation = 'delete_many', allow_empty_filter = 1 })\n"
        "return tostring(ok) .. '|' .. tostring(err)",
        "=data_service_filter_test",
        &error,
        &result));

    REQUIRE(result == "false|db_send_request: allow_empty_filter must be boolean");
}

TEST_CASE("db_send_request validates max_result_documents type", "[database][data_service]") {
    ScriptVM vm;
    script::ExportDbService(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local ok, err = db_send_request({ operation = 'find', max_result_documents = 1.5 })\n"
        "return tostring(ok) .. '|' .. tostring(err)",
        "=data_service_max_result_test",
        &error,
        &result));

    REQUIRE(result == "false|db_send_request: max_result_documents must be an integer");
}

TEST_CASE("db_next_request_id returns increasing ids", "[database][data_service]") {
    ScriptVM vm;
    script::ExportDbService(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local a = db_next_request_id()\n"
        "local b = db_next_request_id()\n"
        "return tostring(b > a)",
        "=data_service_request_id_test",
        &error,
        &result));

    REQUIRE(result == "true");
}
