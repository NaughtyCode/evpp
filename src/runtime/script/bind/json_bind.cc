#include "runtime/script/bind/json_bind.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/json.hpp"
#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/bind/bind_util.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

using JsonValue = glz::generic_i64;
using JsonArray = JsonValue::array_t;
using JsonObject = JsonValue::object_t;

constexpr int kMaxJsonDepth = 256;
constexpr char kArrayMetaName[] = "engine.json.array";
constexpr char kObjectMetaName[] = "engine.json.object";

char kJsonNullSentinel;

struct JsonOptions {
	bool comments = false;
	bool pretty = false;
};

struct TableShape {
	bool dense_array = true;
	bool empty = true;
	size_t length = 0;
	size_t count = 0;
};

void PushStdString(lua_State* L, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
}

void PushJsonNull(lua_State* L) {
	lua_pushlightuserdata(L, &kJsonNullSentinel);
}

bool IsJsonNull(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   lua_touserdata(L, index) == &kJsonNullSentinel;
}

bool ReadBoolField(lua_State* L, int index, const char* field) {
	lua_getfield(L, index, field);
	const bool value = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	return value;
}

bool ReadOptions(lua_State* L, int index, JsonOptions& options, std::string& error) {
	if (index > lua_gettop(L) || lua_isnoneornil(L, index)) {
		return true;
	}

	if (!lua_istable(L, index)) {
		error = "json options must be a table";
		return false;
	}

	const int abs_index = lua_absindex(L, index);
	options.comments = ReadBoolField(L, abs_index, "comments");
	options.pretty = ReadBoolField(L, abs_index, "pretty") || ReadBoolField(L, abs_index, "prettify");
	return true;
}

std::string FormatGlazeError(const char* prefix, const glz::error_ctx& ec, const std::string& input) {
	return std::string(prefix) + ": " + glz::format_error(ec, input);
}

std::string FormatGlazeError(const char* prefix, const glz::error_ctx& ec) {
	return std::string(prefix) + ": " + glz::format_error(ec);
}

void RegisterJsonMetatable(lua_State* L, const char* name) {
	if (luaL_newmetatable(L, name)) {
		lua_pushstring(L, name);
		lua_setfield(L, -2, "__name");
		lua_pushstring(L, name);
		lua_setfield(L, -2, "__metatable");
	}
	lua_pop(L, 1);
}

void EnsureJsonMetatables(lua_State* L) {
	RegisterJsonMetatable(L, kArrayMetaName);
	RegisterJsonMetatable(L, kObjectMetaName);
}

void SetJsonMetatable(lua_State* L, int index, const char* name) {
	const int abs_index = lua_absindex(L, index);
	luaL_getmetatable(L, name);
	lua_setmetatable(L, abs_index);
}

bool HasJsonMetatable(lua_State* L, int index, const char* name) {
	const int abs_index = lua_absindex(L, index);
	if (!lua_getmetatable(L, abs_index)) {
		return false;
	}
	luaL_getmetatable(L, name);
	const bool same = lua_rawequal(L, -1, -2) != 0;
	lua_pop(L, 2);
	return same;
}

bool InspectTableShape(lua_State* L, int index, TableShape& shape, std::string& error) {
	const int abs_index = lua_absindex(L, index);
	if (!lua_checkstack(L, 2)) {
		error = "json encode: Lua stack overflow while inspecting table";
		return false;
	}

	lua_pushnil(L);
	while (lua_next(L, abs_index) != 0) {
		shape.empty = false;
		++shape.count;

		lua_Integer key = 0;
		if (!lua_isinteger(L, -2) || (key = lua_tointeger(L, -2)) <= 0) {
			shape.dense_array = false;
		} else if (static_cast<uint64_t>(key) >
				   static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
			shape.dense_array = false;
		} else {
			shape.length = std::max(shape.length, static_cast<size_t>(key));
		}

		lua_pop(L, 1);
	}

	if (shape.dense_array && shape.length != shape.count) {
		shape.dense_array = false;
	}
	return true;
}

bool IsMarkedOrDenseArray(lua_State* L, int index) {
	if (!lua_istable(L, index)) {
		return false;
	}
	if (HasJsonMetatable(L, index, kArrayMetaName)) {
		return true;
	}
	if (HasJsonMetatable(L, index, kObjectMetaName)) {
		return false;
	}

	TableShape shape;
	std::string error;
	if (!InspectTableShape(L, index, shape, error)) {
		return false;
	}
	return !shape.empty && shape.dense_array;
}

bool IsMarkedOrInferredObject(lua_State* L, int index) {
	if (!lua_istable(L, index)) {
		return false;
	}
	if (HasJsonMetatable(L, index, kObjectMetaName)) {
		return true;
	}
	if (HasJsonMetatable(L, index, kArrayMetaName)) {
		return false;
	}
	return !IsMarkedOrDenseArray(L, index);
}

bool PushJsonValue(lua_State* L, const JsonValue& value, std::string& error, int depth);

bool PushJsonArray(lua_State* L, const JsonArray& array, std::string& error, int depth) {
	if (array.size() > static_cast<size_t>(INT_MAX)) {
		error = "json decode: array is too large for Lua";
		return false;
	}
	if (!lua_checkstack(L, 2)) {
		error = "json decode: Lua stack overflow while pushing array";
		return false;
	}

	lua_createtable(L, static_cast<int>(array.size()), 0);
	SetJsonMetatable(L, -1, kArrayMetaName);

	for (size_t i = 0; i < array.size(); ++i) {
		if (!PushJsonValue(L, array[i], error, depth + 1)) {
			return false;
		}
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	return true;
}

bool PushJsonObject(lua_State* L, const JsonObject& object, std::string& error, int depth) {
	if (object.size() > static_cast<size_t>(INT_MAX)) {
		error = "json decode: object is too large for Lua";
		return false;
	}
	if (!lua_checkstack(L, 3)) {
		error = "json decode: Lua stack overflow while pushing object";
		return false;
	}

	lua_createtable(L, 0, static_cast<int>(object.size()));
	SetJsonMetatable(L, -1, kObjectMetaName);

	for (const auto& [key, child] : object) {
		lua_pushlstring(L, key.data(), key.size());
		if (!PushJsonValue(L, child, error, depth + 1)) {
			return false;
		}
		lua_settable(L, -3);
	}
	return true;
}

bool PushJsonValue(lua_State* L, const JsonValue& value, std::string& error, int depth) {
	if (depth > kMaxJsonDepth) {
		error = "json decode: maximum nesting depth exceeded";
		return false;
	}

	if (value.is_null()) {
		PushJsonNull(L);
		return true;
	}
	if (value.is_boolean()) {
		lua_pushboolean(L, value.get_boolean() ? 1 : 0);
		return true;
	}
	if (value.is_string()) {
		const auto& string = value.get_string();
		lua_pushlstring(L, string.data(), string.size());
		return true;
	}
	if (value.is_int64()) {
		lua_pushinteger(L, static_cast<lua_Integer>(value.get<int64_t>()));
		return true;
	}
	if (value.is_double()) {
		lua_pushnumber(L, static_cast<lua_Number>(value.get<double>()));
		return true;
	}
	if (value.is_array()) {
		return PushJsonArray(L, value.get_array(), error, depth);
	}
	if (value.is_object()) {
		return PushJsonObject(L, value.get_object(), error, depth);
	}

	error = "json decode: unsupported Glaze generic value";
	return false;
}

bool LuaNumberIsFinite(lua_State* L, int index) {
	return std::isfinite(static_cast<double>(lua_tonumber(L, index)));
}

std::string LuaKeyToString(lua_State* L, int index, bool& ok) {
	ok = true;
	switch (lua_type(L, index)) {
	case LUA_TSTRING: {
		size_t len = 0;
		const char* str = lua_tolstring(L, index, &len);
		return std::string(str ? str : "", len);
	}
	case LUA_TNUMBER:
		if (!LuaNumberIsFinite(L, index)) {
			ok = false;
			return {};
		}
		if (lua_isinteger(L, index)) {
			return std::to_string(static_cast<int64_t>(lua_tointeger(L, index)));
		} else {
			std::ostringstream oss;
			oss << std::setprecision(std::numeric_limits<double>::max_digits10)
				<< static_cast<double>(lua_tonumber(L, index));
			return oss.str();
		}
	case LUA_TBOOLEAN:
		return lua_toboolean(L, index) ? "true" : "false";
	default:
		ok = false;
		return {};
	}
}

bool ConvertLuaToJson(lua_State* L,
					  int index,
					  JsonValue& out,
					  std::vector<const void*>& table_stack,
					  std::string& error,
					  int depth);

bool ConvertLuaArray(lua_State* L,
					 int index,
					 size_t length,
					 JsonValue& out,
					 std::vector<const void*>& table_stack,
					 std::string& error,
					 int depth) {
	if (length > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		error = "json encode: array is too large";
		return false;
	}
	if (!lua_checkstack(L, 1)) {
		error = "json encode: Lua stack overflow while reading array";
		return false;
	}

	const int abs_index = lua_absindex(L, index);
	JsonArray array;
	array.reserve(length);
	for (size_t i = 1; i <= length; ++i) {
		lua_rawgeti(L, abs_index, static_cast<lua_Integer>(i));
		JsonValue child;
		const bool ok =
			ConvertLuaToJson(L, -1, child, table_stack, error, depth + 1);
		lua_pop(L, 1);
		if (!ok) {
			return false;
		}
		array.push_back(std::move(child));
	}

	out = std::move(array);
	return true;
}

bool ConvertLuaObject(lua_State* L,
					  int index,
					  JsonValue& out,
					  std::vector<const void*>& table_stack,
					  std::string& error,
					  int depth) {
	if (!lua_checkstack(L, 3)) {
		error = "json encode: Lua stack overflow while reading object";
		return false;
	}

	const int abs_index = lua_absindex(L, index);
	JsonObject object;

	lua_pushnil(L);
	while (lua_next(L, abs_index) != 0) {
		bool key_ok = false;
		std::string key = LuaKeyToString(L, -2, key_ok);
		if (!key_ok) {
			lua_pop(L, 2);
			error = "json encode: object keys must be string, number, or boolean";
			return false;
		}

		JsonValue child;
		const bool value_ok =
			ConvertLuaToJson(L, -1, child, table_stack, error, depth + 1);
		lua_pop(L, 1);
		if (!value_ok) {
			lua_pop(L, 1);
			return false;
		}

		auto [it, inserted] = object.insert(std::make_pair(std::move(key), std::move(child)));
		(void)it;
		if (!inserted) {
			lua_pop(L, 1);
			error = "json encode: duplicate object key after string conversion";
			return false;
		}
	}

	out = std::move(object);
	return true;
}

bool ConvertLuaTable(lua_State* L,
					 int index,
					 JsonValue& out,
					 std::vector<const void*>& table_stack,
					 std::string& error,
					 int depth) {
	const int abs_index = lua_absindex(L, index);
	const void* table_ptr = lua_topointer(L, abs_index);
	if (std::find(table_stack.begin(), table_stack.end(), table_ptr) != table_stack.end()) {
		error = "json encode: table cycle detected";
		return false;
	}

	TableShape shape;
	if (!InspectTableShape(L, abs_index, shape, error)) {
		return false;
	}

	const bool marked_array = HasJsonMetatable(L, abs_index, kArrayMetaName);
	const bool marked_object = HasJsonMetatable(L, abs_index, kObjectMetaName);

	if (marked_array && !shape.dense_array) {
		error = "json encode: marked array tables must use dense positive integer keys";
		return false;
	}

	const bool encode_as_array = marked_array || (!marked_object && !shape.empty && shape.dense_array);

	table_stack.push_back(table_ptr);
	const bool ok = encode_as_array
						? ConvertLuaArray(L, abs_index, shape.length, out, table_stack, error, depth)
						: ConvertLuaObject(L, abs_index, out, table_stack, error, depth);
	table_stack.pop_back();
	return ok;
}

bool ConvertLuaToJson(lua_State* L,
					  int index,
					  JsonValue& out,
					  std::vector<const void*>& table_stack,
					  std::string& error,
					  int depth) {
	if (depth > kMaxJsonDepth) {
		error = "json encode: maximum nesting depth exceeded";
		return false;
	}

	switch (lua_type(L, index)) {
	case LUA_TNONE:
	case LUA_TNIL:
		out = nullptr;
		return true;
	case LUA_TBOOLEAN:
		out = lua_toboolean(L, index) != 0;
		return true;
	case LUA_TSTRING: {
		size_t len = 0;
		const char* str = lua_tolstring(L, index, &len);
		out = std::string(str ? str : "", len);
		return true;
	}
	case LUA_TNUMBER:
		if (!LuaNumberIsFinite(L, index)) {
			error = "json encode: NaN and Infinity are not valid JSON numbers";
			return false;
		}
		if (lua_isinteger(L, index)) {
			out = static_cast<int64_t>(lua_tointeger(L, index));
		} else {
			out = static_cast<double>(lua_tonumber(L, index));
		}
		return true;
	case LUA_TLIGHTUSERDATA:
		if (IsJsonNull(L, index)) {
			out = nullptr;
			return true;
		}
		error = "json encode: unsupported lightuserdata value";
		return false;
	case LUA_TTABLE:
		return ConvertLuaTable(L, index, out, table_stack, error, depth);
	default:
		error = std::string("json encode: unsupported Lua type ") + lua_typename(L, lua_type(L, index));
		return false;
	}
}

bool EncodeLuaValue(lua_State* L, int value_index, const JsonOptions& options, std::string& out_json, std::string& error) {
	JsonValue value;
	std::vector<const void*> table_stack;
	if (!ConvertLuaToJson(L, value_index, value, table_stack, error, 0)) {
		return false;
	}

	auto result = glz::write_json(value);
	if (!result) {
		error = FormatGlazeError("json encode", result.error());
		return false;
	}

	out_json = std::move(*result);
	if (options.pretty) {
		out_json = glz::prettify_json(out_json);
	}
	return true;
}

bool DecodeJsonText(lua_State* L, const std::string& input, const JsonOptions& options, std::string& error) {
	const glz::error_ctx validation =
		options.comments ? glz::validate_jsonc(input) : glz::validate_json(input);
	if (validation) {
		error = FormatGlazeError("json decode", validation, input);
		return false;
	}

	JsonValue value;
	const glz::error_ctx read_error =
		options.comments ? glz::read_jsonc(value, input) : glz::read_json(value, input);
	if (read_error) {
		error = FormatGlazeError("json decode", read_error, input);
		return false;
	}

	return PushJsonValue(L, value, error, 0);
}

int RaiseTopJsonError(lua_State* L, const char* fallback) {
	const char* message = lua_tostring(L, -1);
	return LuaError(L, "%s", message ? message : fallback);
}

bool LoadTextFile(const std::string& path, std::string& content, std::string& error) {
	std::ifstream stream(path, std::ios::in | std::ios::binary);
	if (!stream) {
		error = "json load: failed to open file: " + path;
		return false;
	}

	std::ostringstream buffer;
	buffer << stream.rdbuf();
	if (!stream.good() && !stream.eof()) {
		error = "json load: failed to read file: " + path;
		return false;
	}

	content = buffer.str();
	return true;
}

bool SaveTextFile(const std::string& path, const std::string& content, std::string& error) {
	const std::filesystem::path file_path(path);
	const auto parent = file_path.parent_path();
	if (!parent.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			error = "json save: failed to create directory: " + parent.string() + ": " + ec.message();
			return false;
		}
	}

	std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!stream) {
		error = "json save: failed to open file for writing: " + path;
		return false;
	}

	stream.write(content.data(), static_cast<std::streamsize>(content.size()));
	if (!stream) {
		error = "json save: failed to write file: " + path;
		return false;
	}
	return true;
}

int l_json_decode(lua_State* L) {
	size_t len = 0;
	const char* text = luaL_checklstring(L, 1, &len);
	bool ok = false;

	{
		std::string error;
		JsonOptions options;
		if (!ReadOptions(L, 2, options, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			std::string input(text ? text : "", len);
			if (!DecodeJsonText(L, input, options, error)) {
				lua_settop(L, 0);
				PushStdString(L, error);
			} else {
				ok = true;
			}
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json decode failed");
	}
	return 1;
}

int l_json_encode(lua_State* L) {
	if (lua_gettop(L) < 1) {
		return luaL_error(L, "json encode: value expected");
	}

	bool ok = false;

	{
		std::string error;
		std::string output;
		JsonOptions options;
		if (!ReadOptions(L, 2, options, error) || !EncodeLuaValue(L, 1, options, output, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			lua_pushlstring(L, output.data(), output.size());
			ok = true;
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json encode failed");
	}
	return 1;
}

int l_json_load(lua_State* L) {
	size_t path_len = 0;
	const char* path_data = luaL_checklstring(L, 1, &path_len);
	bool ok = false;

	{
		std::string error;
		std::string content;
		JsonOptions options;
		std::string path(path_data ? path_data : "", path_len);
		if (!ReadOptions(L, 2, options, error) || !LoadTextFile(path, content, error) ||
			!DecodeJsonText(L, content, options, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			ok = true;
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json load failed");
	}
	return 1;
}

int l_json_save(lua_State* L) {
	size_t path_len = 0;
	const char* path_data = luaL_checklstring(L, 1, &path_len);
	if (lua_gettop(L) < 2) {
		return luaL_error(L, "json save: value expected");
	}

	bool ok = false;

	{
		std::string error;
		std::string output;
		JsonOptions options;
		std::string path(path_data ? path_data : "", path_len);
		if (!ReadOptions(L, 3, options, error) || !EncodeLuaValue(L, 2, options, output, error) ||
			!SaveTextFile(path, output, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			lua_pushboolean(L, 1);
			ok = true;
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json save failed");
	}
	return 1;
}

int l_json_validate(lua_State* L) {
	size_t len = 0;
	const char* text = luaL_checklstring(L, 1, &len);
	bool call_ok = false;
	int result_count = 0;

	{
		std::string error;
		JsonOptions options;
		if (!ReadOptions(L, 2, options, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			std::string input(text ? text : "", len);
			const glz::error_ctx ec =
				options.comments ? glz::validate_jsonc(input) : glz::validate_json(input);
			if (ec) {
				lua_pushboolean(L, 0);
				PushStdString(L, FormatGlazeError("json validate", ec, input));
				result_count = 2;
			} else {
				lua_pushboolean(L, 1);
				result_count = 1;
			}
			call_ok = true;
		}
	}

	if (!call_ok) {
		return RaiseTopJsonError(L, "json validate failed");
	}
	return result_count;
}

int l_json_minify(lua_State* L) {
	size_t len = 0;
	const char* text = luaL_checklstring(L, 1, &len);
	bool ok = false;

	{
		std::string error;
		JsonOptions options;
		if (!ReadOptions(L, 2, options, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			std::string input(text ? text : "", len);
			const glz::error_ctx ec =
				options.comments ? glz::validate_jsonc(input) : glz::validate_json(input);
			if (ec) {
				lua_settop(L, 0);
				PushStdString(L, FormatGlazeError("json minify", ec, input));
			} else {
				std::string output =
					options.comments ? glz::minify_jsonc(input) : glz::minify_json(input);
				lua_pushlstring(L, output.data(), output.size());
				ok = true;
			}
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json minify failed");
	}
	return 1;
}

int l_json_prettify(lua_State* L) {
	size_t len = 0;
	const char* text = luaL_checklstring(L, 1, &len);
	bool ok = false;

	{
		std::string error;
		JsonOptions options;
		if (!ReadOptions(L, 2, options, error)) {
			lua_settop(L, 0);
			PushStdString(L, error);
		} else {
			std::string input(text ? text : "", len);
			const glz::error_ctx ec =
				options.comments ? glz::validate_jsonc(input) : glz::validate_json(input);
			if (ec) {
				lua_settop(L, 0);
				PushStdString(L, FormatGlazeError("json prettify", ec, input));
			} else {
				std::string output =
					options.comments ? glz::prettify_jsonc(input) : glz::prettify_json(input);
				lua_pushlstring(L, output.data(), output.size());
				ok = true;
			}
		}
	}

	if (!ok) {
		return RaiseTopJsonError(L, "json prettify failed");
	}
	return 1;
}

int l_json_array(lua_State* L) {
	const int argc = lua_gettop(L);
	lua_createtable(L, argc, 0);
	for (int i = 1; i <= argc; ++i) {
		if (lua_isnil(L, i)) {
			PushJsonNull(L);
		} else {
			lua_pushvalue(L, i);
		}
		lua_rawseti(L, -2, i);
	}
	SetJsonMetatable(L, -1, kArrayMetaName);
	return 1;
}

int l_json_object(lua_State* L) {
	if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
		lua_createtable(L, 0, 0);
		SetJsonMetatable(L, -1, kObjectMetaName);
		return 1;
	}
	if (!lua_istable(L, 1)) {
		return luaL_argerror(L, 1, "table expected");
	}

	lua_pushvalue(L, 1);
	SetJsonMetatable(L, -1, kObjectMetaName);
	return 1;
}

int l_json_as_array(lua_State* L) {
	if (!lua_istable(L, 1)) {
		return luaL_argerror(L, 1, "table expected");
	}
	lua_pushvalue(L, 1);
	SetJsonMetatable(L, -1, kArrayMetaName);
	return 1;
}

int l_json_as_object(lua_State* L) {
	if (!lua_istable(L, 1)) {
		return luaL_argerror(L, 1, "table expected");
	}
	lua_pushvalue(L, 1);
	SetJsonMetatable(L, -1, kObjectMetaName);
	return 1;
}

int l_json_is_array(lua_State* L) {
	lua_pushboolean(L, IsMarkedOrDenseArray(L, 1) ? 1 : 0);
	return 1;
}

int l_json_is_object(lua_State* L) {
	lua_pushboolean(L, IsMarkedOrInferredObject(L, 1) ? 1 : 0);
	return 1;
}

int l_json_is_null(lua_State* L) {
	lua_pushboolean(L, (lua_isnil(L, 1) || IsJsonNull(L, 1)) ? 1 : 0);
	return 1;
}

int l_json_type(lua_State* L) {
	if (lua_isnil(L, 1) || IsJsonNull(L, 1)) {
		lua_pushliteral(L, "null");
		return 1;
	}

	switch (lua_type(L, 1)) {
	case LUA_TBOOLEAN:
		lua_pushliteral(L, "boolean");
		break;
	case LUA_TSTRING:
		lua_pushliteral(L, "string");
		break;
	case LUA_TNUMBER:
		lua_pushliteral(L, "number");
		break;
	case LUA_TTABLE:
		lua_pushstring(L, IsMarkedOrDenseArray(L, 1) ? "array" : "object");
		break;
	default:
		lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
		break;
	}
	return 1;
}

const luaL_Reg kJsonFunctions[] = {
	{"decode", l_json_decode},
	{"parse", l_json_decode},
	{"encode", l_json_encode},
	{"stringify", l_json_encode},
	{"load", l_json_load},
	{"read", l_json_load},
	{"read_file", l_json_load},
	{"save", l_json_save},
	{"write", l_json_save},
	{"write_file", l_json_save},
	{"validate", l_json_validate},
	{"minify", l_json_minify},
	{"prettify", l_json_prettify},
	{"array", l_json_array},
	{"object", l_json_object},
	{"as_array", l_json_as_array},
	{"as_object", l_json_as_object},
	{"is_array", l_json_is_array},
	{"is_object", l_json_is_object},
	{"is_null", l_json_is_null},
	{"type", l_json_type},
	{nullptr, nullptr},
};

int l_json_safe(lua_State* L) {
	const int argc = lua_gettop(L);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);

	const int err = lua_pcall(L, argc, LUA_MULTRET, 0);
	if (err == LUA_OK) {
		return lua_gettop(L);
	}

	lua_pushnil(L);
	lua_insert(L, -2);
	return 2;
}

void SetModuleMeta(lua_State* L) {
	lua_pushliteral(L, "json");
	lua_setfield(L, -2, "_NAME");
	lua_pushliteral(L, "glaze-json-lua 1.0.0");
	lua_setfield(L, -2, "_VERSION");
	lua_pushliteral(L, "Lua bindings for Glaze JSON");
	lua_setfield(L, -2, "_DESCRIPTION");
}

void SetModuleConstants(lua_State* L) {
	PushJsonNull(L);
	lua_setfield(L, -2, "null");
}

void ExportJsonModule(lua_State* L, const char* global_name, bool safe) {
	luaL_newlib(L, kJsonFunctions);

	if (safe) {
		for (const luaL_Reg* r = kJsonFunctions; r->name != nullptr; ++r) {
			lua_getfield(L, -1, r->name);
			lua_pushcclosure(L, l_json_safe, 1);
			lua_setfield(L, -2, r->name);
		}
	}

	SetModuleConstants(L);
	SetModuleMeta(L);
	lua_setglobal(L, global_name);
}

}  // namespace

void ExportJson(ScriptVM& vm) {
	ENGINE_PROFILE_SCOPE("engine.script", "ExportJson");
	lua_State* L = vm.GetState();
	if (!L) return;

	EnsureJsonMetatables(L);
	ExportJsonModule(L, "json", false);
	ExportJsonModule(L, "json_safe", true);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ScriptBind: json modules exported "
					"(json + json_safe: decode/encode/load/save/validate/minify/prettify)");
}

}  // namespace script
}  // namespace engine
