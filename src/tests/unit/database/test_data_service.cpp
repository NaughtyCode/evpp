#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log_init.h"

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/data_service/db_service_main_bind.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
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

TEST_CASE("DBScriptVM exports db_bson table codec", "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local codec = require('db_bson')\n"
        "local doc, err = codec.to_bson({\n"
        "  name = 'db', count = 3, price = 1.5, active = true,\n"
        "  nested = { flag = false }, items = { 10, 20 }, missing = codec.null\n"
        "})\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = codec.to_table(doc)\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "return table.concat({\n"
        "  t.name, tostring(t.count), tostring(t.price), tostring(t.active),\n"
        "  tostring(t.nested.flag), tostring(#t.items), tostring(t.items[2]),\n"
        "  tostring(codec.is_null(t.missing))\n"
        "}, '|')",
        "=data_service_bson_codec_test",
        &error,
        &result));

    REQUIRE(result == "db|3|1.5|true|false|2|20|true");
}

TEST_CASE("db_bson serializes tables and BSON docs to Extended JSON",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local oid = '000000000000000000000001'\n"
        "local doc = assert(b.to_bson({\n"
        "  name = 'db', items = b.array({ 1, 2 }), oid = b.oid(oid), count = b.int64(5)\n"
        "}))\n"
        "local relaxed_doc = assert(b.to_json(doc))\n"
        "local relaxed_table = assert(b.to_relaxed_json({\n"
        "  name = 'db', items = b.array({ 1, 2 }), count = b.int64(5)\n"
        "}))\n"
        "local canonical = assert(b.to_canonical_json({ count = b.int64(5) }))\n"
        "local legacy = assert(b.to_legacy_json({ oid = b.oid(oid) }))\n"
        "local array_json = assert(b.to_json(b.array({ 1, 2 })))\n"
        "local bad, bad_err = b.to_json(123)\n"
        "local bad_utf8, bad_utf8_err = b.to_json({ value = string.char(255) })\n"
        "return table.concat({\n"
        "  tostring(relaxed_doc:find('\"name\"') ~= nil),\n"
        "  tostring(relaxed_table:find('\"items\"') ~= nil),\n"
        "  tostring(canonical:find('$numberLong', 1, true) ~= nil),\n"
        "  tostring(legacy:find('$oid', 1, true) ~= nil),\n"
        "  array_json:sub(1, 1),\n"
        "  tostring(bad == nil), tostring(bad_err),\n"
        "  tostring(bad_utf8 == nil), tostring(bad_utf8_err)\n"
        "}, '|')",
        "=data_service_bson_json_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|true|true|true|[|true|db_bson.to_json expects a table or bson.doc|true|"
            "BSON UTF-8 string must be valid UTF-8");
}

TEST_CASE("db_bson preserves explicit arrays, documents, and BSON scalar wrappers",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local oid = '000000000000000000000001'\n"
        "local doc, err = b.to_bson(b.document({\n"
        "  empty_array = b.array({}), numeric_doc = b.document({ [1] = 'one' }),\n"
        "  i32 = b.int32(7), i64 = b.int64(7), dbl = b.double(7),\n"
        "  oid = b.oid(oid), dt = b.datetime(1234567890123), ts = b.timestamp(12, 34),\n"
        "  bin = b.binary(string.char(65, 0, 66), 128), rx = b.regex('^a$', 'i'),\n"
        "  code = b.code('return x', { x = 1 }), sym = b.symbol('sym'),\n"
        "  dec = b.decimal128('123.45'), ptr = b.dbpointer('coll', oid),\n"
        "  undef = b.undefined, min = b.min_key, max = b.max_key\n"
        "}))\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = b.to_table(doc, { preserve_types = true })\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "local doc2, err3 = b.to_bson(t)\n"
        "if not doc2 then return 'ERR3:' .. tostring(err3) end\n"
        "local t2, err4 = b.to_table(doc2, { preserve_types = true })\n"
        "if not t2 then return 'ERR4:' .. tostring(err4) end\n"
        "return table.concat({\n"
        "  b.type(t2.empty_array), tostring(#t2.empty_array),\n"
        "  b.type(t2.numeric_doc), tostring(t2.numeric_doc['1']),\n"
        "  b.type(t2.i32), tostring(t2.i32.value),\n"
        "  b.type(t2.i64), tostring(t2.i64.value),\n"
        "  b.type(t2.dbl), string.format('%.1f', t2.dbl.value),\n"
        "  b.type(t2.oid), t2.oid.value,\n"
        "  b.type(t2.dt), tostring(t2.dt.value),\n"
        "  b.type(t2.ts), tostring(t2.ts.timestamp), tostring(t2.ts.increment),\n"
        "  b.type(t2.bin), tostring(#t2.bin.value), tostring(t2.bin.subtype),\n"
        "  b.type(t2.rx), t2.rx.pattern, t2.rx.options,\n"
        "  b.type(t2.code), b.type(t2.code.scope.x), tostring(t2.code.scope.x.value),\n"
        "  b.type(t2.sym), t2.sym.value,\n"
        "  b.type(t2.dec), t2.dec.value, b.type(t2.ptr), t2.ptr.collection, t2.ptr.oid,\n"
        "  b.type(t2.undef), b.type(t2.min), b.type(t2.max)\n"
        "}, '|')",
        "=data_service_bson_preserve_test",
        &error,
        &result));

    REQUIRE(result ==
            "array|0|document|one|int32|7|int64|7|double|7.0|"
            "oid|000000000000000000000001|datetime|1234567890123|"
            "timestamp|12|34|binary|3|128|regex|^a$|i|code|int32|1|symbol|sym|decimal128|"
            "123.45|dbpointer|coll|000000000000000000000001|undefined|min_key|max_key");
}

TEST_CASE("db_bson rejects ambiguous and invalid BSON table inputs",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local bad_array, array_err = b.to_bson(b.array({ [1] = 'a', [3] = 'c' }))\n"
        "local bad_oid, oid_err = b.oid('not-an-oid')\n"
        "local bad_subtype, subtype_err = b.binary('x', 300)\n"
        "local bad_i32, i32_err = b.int32(2147483648)\n"
        "local bad_symbol = b.symbol('sym')\n"
        "bad_symbol.value = 123\n"
        "local bad_wrapper, wrapper_err = b.to_bson({ sym = bad_symbol })\n"
        "return table.concat({ tostring(bad_array == nil), tostring(array_err),\n"
        "  tostring(bad_oid == nil), tostring(oid_err),\n"
        "  tostring(bad_subtype == nil), tostring(subtype_err),\n"
        "  tostring(bad_i32 == nil), tostring(i32_err),\n"
        "  tostring(bad_wrapper == nil), tostring(wrapper_err) }, '|')",
        "=data_service_bson_invalid_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|Lua table is not a dense 1-based array|true|"
            "ObjectId must be a 24-character hex string|true|"
            "binary subtype must be between 0 and 255|true|"
            "int32 value must be between INT32_MIN and INT32_MAX|true|"
            "symbol value must be a string");
}

TEST_CASE("db_bson rejects malformed BSON arrays when array mode is forced",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local sparse = bson.new()\n"
        "bson.append_int32(sparse, '0', 1)\n"
        "bson.append_int32(sparse, '2', 3)\n"
        "local sparse_table, sparse_err = b.to_table(sparse, true)\n"
        "local noncanonical = bson.new()\n"
        "bson.append_int32(noncanonical, '0', 1)\n"
        "bson.append_int32(noncanonical, '01', 2)\n"
        "local noncanonical_table, noncanonical_err = b.to_table(noncanonical, true)\n"
        "return table.concat({ tostring(sparse_table == nil), tostring(sparse_err),\n"
        "  tostring(noncanonical_table == nil), tostring(noncanonical_err) }, '|')",
        "=data_service_bson_array_key_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|BSON array keys must be dense zero-based indexes|true|"
            "BSON array keys must be dense zero-based indexes");
}

TEST_CASE("db_bson handles argument errors and nested BSON arrays",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local bad_doc, bad_doc_err = b.to_bson(123)\n"
        "local bad_table, bad_table_err = b.to_table(123)\n"
        "local arr_doc = assert(b.to_bson(b.array({ 1, 2 })))\n"
        "local doc, err = b.to_bson({ nested = arr_doc })\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = b.to_table(doc)\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "local preserved, err3 = b.to_table(doc, { preserve_types = true })\n"
        "if not preserved then return 'ERR3:' .. tostring(err3) end\n"
        "return table.concat({ tostring(bad_doc == nil), tostring(bad_doc_err),\n"
        "  tostring(bad_table == nil), tostring(bad_table_err), tostring(#t.nested),\n"
        "  tostring(t.nested[2]), b.type(preserved.nested) }, '|')",
        "=data_service_bson_argument_nested_array_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|db_bson.to_bson expects a table|true|db_bson.to_table expects bson.doc|2|2|"
            "array");
}

TEST_CASE("db_bson preserves BSON doc shape metadata for docs it creates",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local empty_array_doc = assert(b.to_bson(b.array({})))\n"
        "local empty_array_json = assert(b.to_json(empty_array_doc))\n"
        "local empty_array_table = assert(b.to_table(empty_array_doc, { preserve_types = true }))\n"
        "local numeric_doc = assert(b.to_bson(b.document({ [0] = 'zero', [1] = 'one' })))\n"
        "local numeric_json = assert(b.to_json(numeric_doc))\n"
        "local numeric_table = assert(b.to_table(numeric_doc, { preserve_types = true }))\n"
        "local nested_doc = assert(b.to_bson({ arr = empty_array_doc, doc = numeric_doc }))\n"
        "local nested_table = assert(b.to_table(nested_doc, { preserve_types = true }))\n"
        "return table.concat({ tostring(empty_array_json:sub(1, 1) == '['),\n"
        "  b.type(empty_array_table), tostring(#empty_array_table),\n"
        "  tostring(numeric_json:sub(1, 1) == '{'),\n"
        "  b.type(numeric_table), tostring(numeric_table['0']), tostring(numeric_table['1']),\n"
        "  b.type(nested_table.arr), tostring(#nested_table.arr), b.type(nested_table.doc),\n"
        "  tostring(nested_table.doc['0']) }, '|')",
        "=data_service_bson_shape_metadata_test",
        &error,
        &result));

    REQUIRE(result == "true|array|0|true|document|zero|one|array|0|document|zero");
}

TEST_CASE("db_bson can explicitly wrap raw BSON docs as arrays or documents",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local raw_empty = bson.new()\n"
        "local raw_numeric = bson.new()\n"
        "bson.append_int32(raw_numeric, '0', 10)\n"
        "bson.append_int32(raw_numeric, '1', 20)\n"
        "local doc, err = b.to_bson({\n"
        "  empty_arr = b.array(raw_empty), empty_doc = b.document(raw_empty),\n"
        "  forced_arr = b.array(raw_numeric), forced_doc = b.document(raw_numeric),\n"
        "  code = b.code('return x', b.document(raw_numeric))\n"
        "})\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = b.to_table(doc, { preserve_types = true })\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "local root_arr = assert(b.to_bson(b.array(raw_numeric)))\n"
        "local root_table = assert(b.to_table(root_arr, { preserve_types = true }))\n"
        "local json_arr = assert(b.to_json(b.array(raw_empty)))\n"
        "local bad_scope, scope_err = b.to_bson({ code = b.code('return x', b.array(raw_numeric)) })\n"
        "local mixed = b.array(raw_empty)\n"
        "mixed.extra = 1\n"
        "local bad_mixed, mixed_err = b.to_bson({ value = mixed })\n"
        "return table.concat({ b.type(t.empty_arr), tostring(#t.empty_arr),\n"
        "  b.type(t.empty_doc), tostring(t.empty_doc.missing == nil),\n"
        "  b.type(t.forced_arr), tostring(t.forced_arr[1].value),\n"
        "  tostring(t.forced_arr[2].value), b.type(t.forced_doc),\n"
        "  tostring(t.forced_doc['0'].value), tostring(t.forced_doc['1'].value),\n"
        "  b.type(t.code.scope), tostring(t.code.scope['0'].value),\n"
        "  b.type(root_table), tostring(root_table[2].value), json_arr:sub(1, 1),\n"
        "  tostring(bad_scope == nil), tostring(scope_err),\n"
        "  tostring(bad_mixed == nil), tostring(mixed_err) }, '|')",
        "=data_service_bson_raw_doc_wrapper_test",
        &error,
        &result));

    REQUIRE(result ==
            "array|0|document|true|array|10|20|document|10|20|document|10|array|20|[|"
            "true|code scope must be a document table|true|"
            "BSON document wrapper must not contain Lua fields");
}

TEST_CASE("db_bson rejects implicit sparse array tables",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local sparse, sparse_err = b.to_bson({ [1] = 'a', [3] = 'c' })\n"
        "local nested, nested_err = b.to_bson({ values = { [1] = 'a', [3] = 'c' } })\n"
        "local explicit = assert(b.to_bson(b.document({ [1] = 'a', [3] = 'c' })))\n"
        "local explicit_table = assert(b.to_table(explicit, { preserve_types = true }))\n"
        "local scope_array = assert(b.to_bson(b.array({ 1 })))\n"
        "local bad_scope, scope_err = b.to_bson({ code = b.code('return x', scope_array) })\n"
        "return table.concat({ tostring(sparse == nil), tostring(sparse_err),\n"
        "  tostring(nested == nil), tostring(nested_err), b.type(explicit_table),\n"
        "  tostring(explicit_table['1']), tostring(explicit_table['3']),\n"
        "  tostring(bad_scope == nil), tostring(scope_err) }, '|')",
        "=data_service_bson_sparse_array_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|Lua table has sparse positive integer keys; use db_bson.document(...) for "
            "numeric document keys|true|Lua table has sparse positive integer keys; use "
            "db_bson.document(...) for numeric document keys|document|a|c|true|"
            "code scope must be a document table or bson.doc");
}

TEST_CASE("db_bson validates forced JSON array BSON documents",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local sparse = bson.new()\n"
        "bson.append_int32(sparse, '0', 1)\n"
        "bson.append_int32(sparse, '2', 3)\n"
        "local sparse_json, sparse_err = b.to_json(sparse, true)\n"
        "local empty = bson.new()\n"
        "local empty_json, empty_err = b.to_json(empty, true)\n"
        "if not empty_json then return 'ERR:' .. tostring(empty_err) end\n"
        "return table.concat({ tostring(sparse_json == nil), tostring(sparse_err),\n"
        "  empty_json:sub(1, 1) }, '|')",
        "=data_service_bson_forced_json_array_test",
        &error,
        &result));

    REQUIRE(result == "true|BSON array keys must be dense zero-based indexes|[");
}

TEST_CASE("db_bson preserves non-finite doubles", "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local nan = 0 / 0\n"
        "local doc, err = b.to_bson({ inf = math.huge, nan = b.double(nan) })\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = b.to_table(doc, { preserve_types = true })\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "local doc2, err3 = b.to_bson(t)\n"
        "if not doc2 then return 'ERR3:' .. tostring(err3) end\n"
        "local t2, err4 = b.to_table(doc2, { preserve_types = true })\n"
        "if not t2 then return 'ERR4:' .. tostring(err4) end\n"
        "return table.concat({\n"
        "  b.type(t2.inf), tostring(t2.inf.value == math.huge),\n"
        "  b.type(t2.nan), tostring(t2.nan.value ~= t2.nan.value)\n"
        "}, '|')",
        "=data_service_bson_nonfinite_double_test",
        &error,
        &result));

    REQUIRE(result == "double|true|double|true");
}

TEST_CASE("db_bson preserves BSON symbol strings with embedded NUL",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local value = 'a' .. string.char(0) .. 'b'\n"
        "local sym, sym_err = b.symbol(value)\n"
        "if not sym then return 'SYMERR:' .. tostring(sym_err) end\n"
        "local doc, err = b.to_bson({ sym = sym })\n"
        "if not doc then return 'ERR:' .. tostring(err) end\n"
        "local t, err2 = b.to_table(doc, { preserve_types = true })\n"
        "if not t then return 'ERR2:' .. tostring(err2) end\n"
        "local doc2, err3 = b.to_bson(t)\n"
        "if not doc2 then return 'ERR3:' .. tostring(err3) end\n"
        "local t2, err4 = b.to_table(doc2, { preserve_types = true })\n"
        "if not t2 then return 'ERR4:' .. tostring(err4) end\n"
        "return table.concat({ b.type(t2.sym), tostring(#t2.sym.value),\n"
        "  tostring(string.byte(t2.sym.value, 2)), tostring(t2.sym.value == value) }, '|')",
        "=data_service_bson_symbol_nul_test",
        &error,
        &result));

    REQUIRE(result == "symbol|3|0|true");
}

TEST_CASE("db_bson rejects duplicate document keys", "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local lua_dup, lua_dup_err = b.to_bson(b.document({ [1] = 'one', ['1'] = 'string' }))\n"
        "local raw = bson.new()\n"
        "bson.append_int32(raw, 'dup', 1)\n"
        "bson.append_int32(raw, 'dup', 2)\n"
        "local raw_dup, raw_dup_err = b.to_table(raw)\n"
        "local raw_json, raw_json_err = b.to_json(raw)\n"
        "return table.concat({ tostring(lua_dup == nil), tostring(lua_dup_err),\n"
        "  tostring(raw_dup == nil), tostring(raw_dup_err),\n"
        "  tostring(raw_json == nil), tostring(raw_json_err) }, '|')",
        "=data_service_bson_duplicate_key_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|BSON document keys must be unique after conversion|true|"
            "BSON document keys must be unique|true|BSON document keys must be unique");
}

TEST_CASE("db_bson rejects conflicting array option aliases",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local doc = assert(b.to_bson({ x = 1 }))\n"
        "local bad_doc, bad_doc_err = b.to_bson({ 1 }, { root_as_array = false, array = true })\n"
        "local bad_table, bad_table_err = b.to_table(doc, { root_as_array = true, array = false })\n"
        "return table.concat({ tostring(bad_doc == nil), tostring(bad_doc_err),\n"
        "  tostring(bad_table == nil), tostring(bad_table_err) }, '|')",
        "=data_service_bson_conflicting_options_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|options 'root_as_array' and 'array' must match|true|"
            "options 'root_as_array' and 'array' must match");
}

TEST_CASE("db_bson rejects cyclic tables and invalid UTF-8 text",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local recursive = {}\n"
        "recursive.self = recursive\n"
        "local bad_cycle, cycle_err = b.to_bson(recursive)\n"
        "local invalid_utf8 = string.char(255)\n"
        "local bad_text, text_err = b.to_bson({ value = invalid_utf8 })\n"
        "local bad_key_table = {}\n"
        "bad_key_table[invalid_utf8] = 1\n"
        "local bad_key, key_err = b.to_bson(bad_key_table)\n"
        "local bad_symbol, symbol_err = b.symbol(invalid_utf8)\n"
        "local binary = assert(b.binary(invalid_utf8))\n"
        "local doc, doc_err = b.to_bson({ payload = binary })\n"
        "if not doc then return 'ERR:' .. tostring(doc_err) end\n"
        "local t, table_err = b.to_table(doc, { preserve_types = true })\n"
        "if not t then return 'ERR2:' .. tostring(table_err) end\n"
        "return table.concat({ tostring(bad_cycle == nil), tostring(cycle_err),\n"
        "  tostring(bad_text == nil), tostring(text_err),\n"
        "  tostring(bad_key == nil), tostring(key_err),\n"
        "  tostring(bad_symbol == nil), tostring(symbol_err),\n"
        "  b.type(t.payload), tostring(string.byte(t.payload.value, 1)) }, '|')",
        "=data_service_bson_utf8_cycle_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|Lua table cycle detected during BSON conversion|true|"
            "BSON UTF-8 string must be valid UTF-8|true|"
            "BSON document keys must be valid UTF-8|true|"
            "symbol value must be valid UTF-8|binary|255");
}

TEST_CASE("db_bson rejects invalid regex options and invalid BSON text on read",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local bad_regex, bad_regex_err = b.regex('x', 'j')\n"
        "local regex = assert(b.regex('x', 'i'))\n"
        "regex.options = 'j'\n"
        "local bad_wrapper, bad_wrapper_err = b.to_bson({ rx = regex })\n"
        "local raw_regex = bson.from_data(string.char(13, 0, 0, 0, 0x0B,\n"
        "  string.byte('r'), string.byte('x'), 0, string.byte('x'), 0,\n"
        "  string.byte('j'), 0, 0))\n"
        "local bad_raw_regex, bad_raw_regex_err = b.to_table(raw_regex)\n"
        "local bad_raw_regex_json, bad_raw_regex_json_err = b.to_json(raw_regex)\n"
        "local raw = bson.new()\n"
        "bson.append_utf8(raw, 'bad', string.char(255))\n"
        "local bad_table, bad_table_err = b.to_table(raw)\n"
        "local bad_nested, bad_nested_err = b.to_bson({ nested = raw })\n"
        "local bad_scope, bad_scope_err = b.to_bson({ code = b.code('return x', raw) })\n"
        "return table.concat({ tostring(bad_regex == nil), tostring(bad_regex_err),\n"
        "  tostring(bad_wrapper == nil), tostring(bad_wrapper_err),\n"
        "  tostring(bad_raw_regex == nil), tostring(bad_raw_regex_err),\n"
        "  tostring(bad_raw_regex_json == nil), tostring(bad_raw_regex_json_err),\n"
        "  tostring(bad_table == nil),\n"
        "  tostring(tostring(bad_table_err):find('valid UTF%-8') ~= nil),\n"
        "  tostring(bad_nested == nil),\n"
        "  tostring(tostring(bad_nested_err):find('valid UTF%-8') ~= nil),\n"
        "  tostring(bad_scope == nil),\n"
        "  tostring(tostring(bad_scope_err):find('valid UTF%-8') ~= nil) }, '|')",
        "=data_service_bson_regex_validation_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|regex options may only contain i, m, x, l, s, or u|true|"
            "regex options may only contain i, m, x, l, s, or u|true|"
            "regex options may only contain i, m, x, l, s, or u|true|"
            "regex options may only contain i, m, x, l, s, or u|true|true|true|true|true|true");
}

TEST_CASE("db_bson rejects duplicate regex option flags",
          "[database][data_service][bson]") {
    DBScriptVM vm;
    script::ExportMongo(vm);
    ExportDbRuntime(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "local b = require('db_bson')\n"
        "local dup_ctor, dup_ctor_err = b.regex('x', 'ii')\n"
        "local regex = assert(b.regex('x', 'i'))\n"
        "regex.options = 'ss'\n"
        "local dup_wrapper, dup_wrapper_err = b.to_bson({ rx = regex })\n"
        "local raw = bson.from_data(string.char(14, 0, 0, 0, 0x0B,\n"
        "  string.byte('r'), string.byte('x'), 0, string.byte('x'), 0,\n"
        "  string.byte('s'), string.byte('s'), 0, 0))\n"
        "local dup_raw, dup_raw_err = b.to_table(raw)\n"
        "local dup_json, dup_json_err = b.to_json(raw)\n"
        "return table.concat({ tostring(dup_ctor == nil), tostring(dup_ctor_err),\n"
        "  tostring(dup_wrapper == nil), tostring(dup_wrapper_err),\n"
        "  tostring(dup_raw == nil), tostring(dup_raw_err),\n"
        "  tostring(dup_json == nil), tostring(dup_json_err) }, '|')",
        "=data_service_bson_duplicate_regex_options_test",
        &error,
        &result));

    REQUIRE(result ==
            "true|regex options must not contain duplicate flags|true|"
            "regex options must not contain duplicate flags|true|"
            "regex options must not contain duplicate flags|true|"
            "regex options must not contain duplicate flags");
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
