#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "log_init.h"
#include "runtime/script/bind/json_bind.h"
#include "runtime/vm/vm.h"

using namespace engine;

struct JsonBindFixture {
	ScriptVM vm;

	JsonBindFixture() {
		script::ExportJson(vm);
	}

	bool RunLua(const std::string& code, std::string* error = nullptr) {
		return vm.DoString(code, "test_json_bind", error);
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_json_bind", nullptr, &result);
	}

	void SetGlobalStr(const std::string& name, const std::string& value) {
		vm.SetGlobal<std::string_view>(name, value);
	}
};

struct TempJsonFile {
	std::filesystem::path path;

	TempJsonFile() {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = std::filesystem::temp_directory_path() /
			   ("evpp2_json_bind_" + std::to_string(stamp) + ".json");
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	~TempJsonFile() {
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
};

TEST_CASE("json modules are exported", "[json_bind][module]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"return type(json) .. ',' .. type(json.decode) .. ',' .. type(json_safe) .. ',' .. "
		"type(json_safe.decode) .. ',' .. json.type(json.null)",
		result));
	REQUIRE(result == "table,function,table,function,null");
}

TEST_CASE("json decode preserves object, array, and null shape", "[json_bind][decode]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local v = json.decode([[{"name":"evpp","n":42,"arr":[1,true,null],"emptyArray":[],"emptyObject":{}}]])
return v.name .. ',' ..
       tostring(v.n) .. ',' ..
       tostring(v.arr[2]) .. ',' ..
       json.type(v.arr[3]) .. ',' ..
       json.type(v.emptyArray) .. ',' ..
       json.type(v.emptyObject)
)lua",
		result));
	REQUIRE(result == "evpp,42,true,null,array,object");
}

TEST_CASE("json encode round trips Lua tables through Glaze JSON", "[json_bind][encode]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local text = json.encode({
  name = "evpp",
  enabled = true,
  nums = json.array(1, 2, 3),
  none = json.null,
})
local v = json.decode(text)
return v.name .. ',' .. tostring(v.enabled) .. ',' .. tostring(v.nums[3]) .. ',' .. json.type(v.none)
)lua",
		result));
	REQUIRE(result == "evpp,true,3,null");
}

TEST_CASE("json constructors disambiguate empty arrays and objects", "[json_bind][shape]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("return json.encode(json.array()) .. '|' .. json.encode(json.object())", result));
	REQUIRE(result == "[]|{}");
}

TEST_CASE("json array constructor keeps nil arguments as JSON null", "[json_bind][null]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult("return json.encode(json.array(1, nil, 3))", result));
	REQUIRE(result == "[1,null,3]");
}

TEST_CASE("json helpers classify marked and inferred values", "[json_bind][helpers]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local a = json.array("x")
local o = json.object({x = 1})
local dense = {1, 2}
local mixed = {x = 1}
return tostring(json.is_array(a)) .. ',' ..
       tostring(json.is_object(o)) .. ',' ..
       json.type(dense) .. ',' ..
       json.type(mixed) .. ',' ..
       tostring(json.is_null(json.null))
)lua",
		result));
	REQUIRE(result == "true,true,array,object,true");
}

TEST_CASE("json validate, minify, and prettify expose Glaze JSON APIs", "[json_bind][format]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local ok = json.validate('{"a":1}')
local bad, err = json.validate('{')
local mini = json.minify(' { "a" : [ 1, 2 ] } ')
local pretty = json.prettify(mini)
return tostring(ok) .. ',' ..
       tostring(bad) .. ',' ..
       tostring(type(err) == 'string') .. ',' ..
       mini .. ',' ..
       tostring(pretty:find('\n') ~= nil)
)lua",
		result));
	REQUIRE(result == R"(true,false,true,{"a":[1,2]},true)");
}

TEST_CASE("json supports JSONC parsing when comments option is enabled", "[json_bind][jsonc]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"local v = json.decode('{/* comment */\"a\":1}', {comments = true}); return tostring(v.a)",
		result));
	REQUIRE(result == "1");
}

TEST_CASE("json_safe turns binding errors into nil plus message", "[json_bind][safe]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"local v, err = json_safe.decode('{'); return tostring(v) .. ',' .. type(err) .. ',' .. "
		"tostring(err:find('json decode') ~= nil)",
		result));
	REQUIRE(result == "nil,string,true");
}

TEST_CASE("json save and load round trip data through files", "[json_bind][file]") {
	JsonBindFixture f;
	TempJsonFile temp;
	f.SetGlobalStr("json_path", temp.path.string());

	std::string result;
	REQUIRE(f.RunLuaResult(
		R"lua(
local payload = json.object({
  name = "file",
  items = json.array(1, 2, json.null),
})
assert(json.save(json_path, payload, {pretty = true}))
local loaded = json.load(json_path)
return loaded.name .. ',' .. tostring(loaded.items[2]) .. ',' .. json.type(loaded.items[3])
)lua",
		result));
	REQUIRE(result == "file,2,null");

	std::ifstream stream(temp.path, std::ios::in | std::ios::binary);
	REQUIRE(stream.good());
	const std::string content((std::istreambuf_iterator<char>(stream)),
							  std::istreambuf_iterator<char>());
	REQUIRE(content.find('\n') != std::string::npos);
}

TEST_CASE("json encode rejects circular tables", "[json_bind][error]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"local t = {}; t.self = t; local ok, err = pcall(function() json.encode(t) end); "
		"return tostring(ok) .. ',' .. tostring(err:find('cycle') ~= nil)",
		result));
	REQUIRE(result == "false,true");
}

TEST_CASE("json encode rejects sparse marked arrays", "[json_bind][error]") {
	JsonBindFixture f;
	std::string result;
	REQUIRE(f.RunLuaResult(
		"local t = {}; t[1] = 'a'; t[3] = 'c'; json.as_array(t); "
		"local ok, err = pcall(function() json.encode(t) end); "
		"return tostring(ok) .. ',' .. tostring(err:find('dense positive integer') ~= nil)",
		result));
	REQUIRE(result == "false,true");
}
