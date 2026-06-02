#if defined(ENGINE_MONGODB_ENABLED)

// Provides the db_bson Lua module for DBScriptVM.
//
// Purpose:
// - Convert plain Lua tables to mongo::BsonDocument values used by database requests.
// - Convert Lua tables or BSON documents to Extended JSON for database request payloads.
// - Convert BSON documents back to Lua tables for script-side inspection.
// - Preserve BSON-only types through lightweight tagged Lua tables and sentinel values when
//   requested, while keeping the default API convenient for primitive tables.
// - Resolve Lua table ambiguity by allowing scripts to explicitly mark array vs document tables.

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/bson_table_codec.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

#include <bson/bson.h>

#include "runtime/core/mem/mem.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_oid.h"
#include "runtime/database/mongo_bind/bind_bson_document.h"
#include "runtime/database/mongo_bind/bind_util.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace {

constexpr const char* kModuleName = "db_bson";
constexpr const char* kBsonDocMetaName = "bson.doc";
constexpr int kDefaultMaxDepth = 64;
constexpr int kDefaultMaxConfigurableDepth = 256;
constexpr int kDefaultLuaStackReserve = 16;
constexpr int kHardMaxConfigurableDepth = 4096;
constexpr int kHardMaxLuaStackReserve = 256;
constexpr const char* kSparseArrayError =
	"Lua table has sparse positive integer keys; use db_bson.document(...) for numeric document keys";

char kNullSentinel;
char kUndefinedSentinel;
char kMinKeySentinel;
char kMaxKeySentinel;
char kTypeKey;
char kValueKey;
char kSubtypeKey;
char kOptionsKey;
char kIncrementKey;
char kScopeKey;
char kOidKey;
char kBsonDocKey;

enum class WrapperType : int {
	None = 0,
	Array,
	Document,
	Oid,
	DateTime,
	Timestamp,
	Binary,
	Regex,
	Code,
	Symbol,
	Decimal128,
	DBPointer,
	Int32,
	Int64,
	Double,
};

struct TableOptions {
	bool root_as_array = false;
	bool root_as_array_set = false;
	bool preserve_types = false;
	int max_depth = kDefaultMaxDepth;
	int max_configurable_depth = kDefaultMaxConfigurableDepth;
	int lua_stack_reserve = kDefaultLuaStackReserve;
};

enum class JsonMode {
	Relaxed,
	Canonical,
	Legacy,
};

enum class LuaTableShape {
	Empty,
	DenseArray,
	SparseArray,
	Document,
};

enum class WrapperBsonDocumentResult {
	NotPresent,
	Copied,
	Error,
};

struct ActiveTableGuard {
	std::unordered_set<const void*>* active_tables = nullptr;
	const void* table = nullptr;
	bool inserted = false;

	ActiveTableGuard(std::unordered_set<const void*>* active, const void* table_pointer)
		: active_tables(active), table(table_pointer) {
	}

	~ActiveTableGuard() {
		if (inserted && active_tables) {
			active_tables->erase(table);
		}
	}
};

const char* WrapperTypeName(WrapperType type) {
	switch (type) {
	case WrapperType::Array: return "array";
	case WrapperType::Document: return "document";
	case WrapperType::Oid: return "oid";
	case WrapperType::DateTime: return "datetime";
	case WrapperType::Timestamp: return "timestamp";
	case WrapperType::Binary: return "binary";
	case WrapperType::Regex: return "regex";
	case WrapperType::Code: return "code";
	case WrapperType::Symbol: return "symbol";
	case WrapperType::Decimal128: return "decimal128";
	case WrapperType::DBPointer: return "dbpointer";
	case WrapperType::Int32: return "int32";
	case WrapperType::Int64: return "int64";
	case WrapperType::Double: return "double";
	default: return nullptr;
	}
}

bool IsInternalKeyPointer(const void* ptr) {
	return ptr == &kTypeKey || ptr == &kValueKey || ptr == &kSubtypeKey ||
		   ptr == &kOptionsKey || ptr == &kIncrementKey || ptr == &kScopeKey ||
		   ptr == &kOidKey || ptr == &kBsonDocKey;
}

bool IsInternalTableKey(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   IsInternalKeyPointer(lua_touserdata(L, index));
}

bool PushInteger64(lua_State* L, int64_t value);

void PushNullSentinel(lua_State* L) {
	lua_pushlightuserdata(L, &kNullSentinel);
}

void PushUndefinedSentinel(lua_State* L) {
	lua_pushlightuserdata(L, &kUndefinedSentinel);
}

void PushMinKeySentinel(lua_State* L) {
	lua_pushlightuserdata(L, &kMinKeySentinel);
}

void PushMaxKeySentinel(lua_State* L) {
	lua_pushlightuserdata(L, &kMaxKeySentinel);
}

bool IsNullSentinel(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   lua_touserdata(L, index) == &kNullSentinel;
}

bool IsUndefinedSentinel(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   lua_touserdata(L, index) == &kUndefinedSentinel;
}

bool IsMinKeySentinel(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   lua_touserdata(L, index) == &kMinKeySentinel;
}

bool IsMaxKeySentinel(lua_State* L, int index) {
	return lua_type(L, index) == LUA_TLIGHTUSERDATA &&
		   lua_touserdata(L, index) == &kMaxKeySentinel;
}

void SetWrapperType(lua_State* L, int table_index, WrapperType type) {
	table_index = lua_absindex(L, table_index);
	lua_pushinteger(L, static_cast<lua_Integer>(type));
	lua_rawsetp(L, table_index, &kTypeKey);
}

WrapperType GetWrapperType(lua_State* L, int index) {
	if (!lua_istable(L, index)) return WrapperType::None;
	index = lua_absindex(L, index);

	lua_rawgetp(L, index, &kTypeKey);
	WrapperType type = WrapperType::None;
	if (lua_isinteger(L, -1)) {
		const auto raw = static_cast<int>(lua_tointeger(L, -1));
		if (raw >= static_cast<int>(WrapperType::Array) &&
			raw <= static_cast<int>(WrapperType::Double)) {
			type = static_cast<WrapperType>(raw);
		}
	}
	lua_pop(L, 1);
	return type;
}

void SetRawStringValue(lua_State* L, int table_index, const void* key, std::string_view value) {
	table_index = lua_absindex(L, table_index);
	lua_pushlstring(L, value.empty() ? "" : value.data(), value.size());
	lua_rawsetp(L, table_index, key);
}

void SetRawIntegerValue(lua_State* L, int table_index, const void* key, int64_t value) {
	table_index = lua_absindex(L, table_index);
	PushInteger64(L, value);
	lua_rawsetp(L, table_index, key);
}

void SetRawNumberValue(lua_State* L, int table_index, const void* key, double value) {
	table_index = lua_absindex(L, table_index);
	lua_pushnumber(L, static_cast<lua_Number>(value));
	lua_rawsetp(L, table_index, key);
}

void SetRawBsonDocumentValue(lua_State* L, int table_index, int doc_index) {
	table_index = lua_absindex(L, table_index);
	doc_index = lua_absindex(L, doc_index);
	lua_pushvalue(L, doc_index);
	lua_rawsetp(L, table_index, &kBsonDocKey);
}

bool GetRawStringValue(lua_State* L,
					   int table_index,
					   const void* key,
					   std::string* out,
					   std::string& error,
					   const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_rawgetp(L, table_index, key);
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		error = std::string(label) + " must be a string";
		return false;
	}

	size_t len = 0;
	const char* data = lua_tolstring(L, -1, &len);
	out->assign(data, len);
	lua_pop(L, 1);
	return true;
}

bool GetRawIntegerValue(lua_State* L,
						int table_index,
						const void* key,
						int64_t* out,
						std::string& error,
						const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_rawgetp(L, table_index, key);
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		error = std::string(label) + " must be an integer";
		return false;
	}

	*out = static_cast<int64_t>(lua_tointeger(L, -1));
	lua_pop(L, 1);
	return true;
}

bool GetRawNumberValue(lua_State* L,
					   int table_index,
					   const void* key,
					   double* out,
					   std::string& error,
					   const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_rawgetp(L, table_index, key);
	if (lua_type(L, -1) != LUA_TNUMBER) {
		lua_pop(L, 1);
		error = std::string(label) + " must be a number";
		return false;
	}

	*out = static_cast<double>(lua_tonumber(L, -1));
	lua_pop(L, 1);
	return true;
}

bool GetRawBsonDocumentValue(lua_State* L,
							 int table_index,
							 mongo::BsonDocument** out,
							 bool* found,
							 std::string& error) {
	table_index = lua_absindex(L, table_index);
	lua_rawgetp(L, table_index, &kBsonDocKey);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		*out = nullptr;
		*found = false;
		return true;
	}

	auto** doc = static_cast<mongo::BsonDocument**>(luaL_testudata(L, -1, kBsonDocMetaName));
	if (!doc || !*doc) {
		lua_pop(L, 1);
		error = "BSON document wrapper contains an invalid bson.doc";
		return false;
	}

	*out = *doc;
	*found = true;
	lua_pop(L, 1);
	return true;
}

void PushTaggedTable(lua_State* L, WrapperType type) {
	lua_newtable(L);
	SetWrapperType(L, -1, type);
}

bool PushInteger64(lua_State* L, int64_t value) {
	if (value >= static_cast<int64_t>(std::numeric_limits<lua_Integer>::min()) &&
		value <= static_cast<int64_t>(std::numeric_limits<lua_Integer>::max())) {
		lua_pushinteger(L, static_cast<lua_Integer>(value));
		return true;
	}
	lua_pushnumber(L, static_cast<lua_Number>(value));
	return true;
}

void SetStringField(lua_State* L, const char* key, const char* value) {
	lua_pushstring(L, value ? value : "");
	lua_setfield(L, -2, key);
}

void SetStringField(lua_State* L, const char* key, std::string_view value) {
	lua_pushlstring(L, value.empty() ? "" : value.data(), value.size());
	lua_setfield(L, -2, key);
}

void SetIntegerField(lua_State* L, const char* key, int64_t value) {
	PushInteger64(L, value);
	lua_setfield(L, -2, key);
}

void SetWrapperStringField(lua_State* L,
						   int table_index,
						   const char* field,
						   const void* raw_key,
						   std::string_view value) {
	table_index = lua_absindex(L, table_index);
	SetRawStringValue(L, table_index, raw_key, value);
	lua_pushlstring(L, value.empty() ? "" : value.data(), value.size());
	lua_setfield(L, table_index, field);
}

void SetWrapperIntegerField(lua_State* L,
							int table_index,
							const char* field,
							const void* raw_key,
							int64_t value) {
	table_index = lua_absindex(L, table_index);
	SetRawIntegerValue(L, table_index, raw_key, value);
	PushInteger64(L, value);
	lua_setfield(L, table_index, field);
}

void SetWrapperNumberField(lua_State* L,
						   int table_index,
						   const char* field,
						   const void* raw_key,
						   double value) {
	table_index = lua_absindex(L, table_index);
	SetRawNumberValue(L, table_index, raw_key, value);
	lua_pushnumber(L, static_cast<lua_Number>(value));
	lua_setfield(L, table_index, field);
}

bool StringHasEmbeddedNull(std::string_view value) {
	if (value.empty()) return false;
	return std::memchr(value.data(), '\0', value.size()) != nullptr;
}

bool EnsureNoEmbeddedNull(std::string_view value, const char* label, std::string& error) {
	if (!StringHasEmbeddedNull(value)) return true;
	error = std::string(label) + " must not contain embedded NUL bytes";
	return false;
}

const char* StringViewDataOrEmpty(std::string_view value) {
	return value.empty() ? "" : value.data();
}

bool EnsureValidUtf8(std::string_view value,
					 bool allow_null,
					 const char* label,
					 std::string& error) {
	if (bson_utf8_validate(StringViewDataOrEmpty(value), value.size(), allow_null)) return true;
	error = std::string(label) + " must be valid UTF-8";
	return false;
}

bool ValidateBsonIntLength(size_t length, const char* label, std::string& error) {
	if (length <= static_cast<size_t>(std::numeric_limits<int>::max())) return true;
	error = std::string(label) + " is too large for BSON";
	return false;
}

bool EnsureValidBsonCString(std::string_view value, const char* label, std::string& error) {
	return ValidateBsonIntLength(value.size(), label, error) &&
		   EnsureNoEmbeddedNull(value, label, error) &&
		   EnsureValidUtf8(value, false, label, error);
}

bool EnsureValidBsonText(std::string_view value, const char* label, std::string& error) {
	return ValidateBsonIntLength(value.size(), label, error) &&
		   EnsureValidUtf8(value, true, label, error);
}

bool EnsureValidRegexOptions(std::string_view options, std::string& error) {
	bool seen[256] = {};
	for (const char option : options) {
		const auto option_index = static_cast<unsigned char>(option);
		switch (option_index) {
		case 'i':
		case 'm':
		case 'x':
		case 'l':
		case 's':
		case 'u':
			break;
		default:
			error = "regex options may only contain i, m, x, l, s, or u";
			return false;
		}
		if (seen[option_index]) {
			error = "regex options must not contain duplicate flags";
			return false;
		}
		seen[option_index] = true;
	}
	return true;
}

bool AppendBsonSymbol(mongo::BsonDocument& parent,
					  const char* key,
					  std::string_view value,
					  std::string& error) {
	if (!ValidateBsonIntLength(value.size(), "symbol value", error)) return false;
	return bson_append_symbol(static_cast<bson_t*>(parent.RawBson()),
							  key,
							  -1,
							  StringViewDataOrEmpty(value),
							  static_cast<int>(value.size()));
}

bool ValidateBsonForLuaConversion(const bson_t* doc, std::string& error) {
	bson_error_t bson_error;
	const auto flags =
		static_cast<bson_validate_flags_t>(BSON_VALIDATE_UTF8 | BSON_VALIDATE_UTF8_ALLOW_NULL);
	if (bson_validate_with_error(doc, flags, &bson_error)) return true;
	error = std::string("invalid BSON document: ") + bson_error.message;
	return false;
}

bool GetWrapperStringField(lua_State* L,
						   int table_index,
						   const char* field,
						   const void* raw_key,
						   std::string* out,
						   std::string& error,
						   const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, field);
	if (lua_type(L, -1) == LUA_TSTRING) {
		size_t len = 0;
		const char* data = lua_tolstring(L, -1, &len);
		out->assign(data, len);
		lua_pop(L, 1);
		return true;
	}

	if (!lua_isnil(L, -1)) {
		lua_pop(L, 1);
		error = std::string(label) + " must be a string";
		return false;
	}

	lua_pop(L, 1);
	return GetRawStringValue(L, table_index, raw_key, out, error, label);
}

bool GetWrapperIntegerField(lua_State* L,
							int table_index,
							const char* field,
							const void* raw_key,
							int64_t* out,
							std::string& error,
							const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, field);
	if (lua_isinteger(L, -1)) {
		*out = static_cast<int64_t>(lua_tointeger(L, -1));
		lua_pop(L, 1);
		return true;
	}

	if (!lua_isnil(L, -1)) {
		lua_pop(L, 1);
		error = std::string(label) + " must be an integer";
		return false;
	}

	lua_pop(L, 1);
	return GetRawIntegerValue(L, table_index, raw_key, out, error, label);
}

bool GetWrapperNumberField(lua_State* L,
						   int table_index,
						   const char* field,
						   const void* raw_key,
						   double* out,
						   std::string& error,
						   const char* label) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, field);
	if (lua_type(L, -1) == LUA_TNUMBER) {
		*out = static_cast<double>(lua_tonumber(L, -1));
		lua_pop(L, 1);
		return true;
	}

	if (!lua_isnil(L, -1)) {
		lua_pop(L, 1);
		error = std::string(label) + " must be a number";
		return false;
	}

	lua_pop(L, 1);
	return GetRawNumberValue(L, table_index, raw_key, out, error, label);
}

bool ValidateInt32(int64_t value, const char* label, int32_t* out, std::string& error) {
	if (value < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
		value > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
		error = std::string(label) + " must be between INT32_MIN and INT32_MAX";
		return false;
	}
	*out = static_cast<int32_t>(value);
	return true;
}

bool ValidateUInt32(int64_t value, const char* label, uint32_t* out, std::string& error) {
	if (value < 0 || value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
		error = std::string(label) + " must be between 0 and UINT32_MAX";
		return false;
	}
	*out = static_cast<uint32_t>(value);
	return true;
}

bool ValidateByte(int64_t value, const char* label, uint8_t* out, std::string& error) {
	if (value < 0 || value > 255) {
		error = std::string(label) + " must be between 0 and 255";
		return false;
	}
	*out = static_cast<uint8_t>(value);
	return true;
}

bool EnsureLuaStack(lua_State* L, int slots, std::string& error) {
	if (lua_checkstack(L, slots)) return true;
	error = "Lua stack limit reached during BSON conversion";
	return false;
}

bool ReadLuaIntegerArgument(lua_State* L,
							int index,
							const char* label,
							int64_t* out,
							std::string& error) {
	if (!lua_isinteger(L, index)) {
		error = std::string(label) + " must be an integer";
		return false;
	}

	*out = static_cast<int64_t>(lua_tointeger(L, index));
	return true;
}

bool ReadOptionalLuaIntegerArgument(lua_State* L,
									int index,
									int64_t default_value,
									const char* label,
									int64_t* out,
									std::string& error) {
	if (lua_isnoneornil(L, index)) {
		*out = default_value;
		return true;
	}
	return ReadLuaIntegerArgument(L, index, label, out, error);
}

bool ReadLuaNumberArgument(lua_State* L,
						   int index,
						   const char* label,
						   double* out,
						   std::string& error) {
	if (lua_type(L, index) != LUA_TNUMBER) {
		error = std::string(label) + " must be a number";
		return false;
	}

	*out = static_cast<double>(lua_tonumber(L, index));
	return true;
}

bool ReadLuaStringArgument(lua_State* L,
						   int index,
						   const char* label,
						   const char** out,
						   size_t* length,
						   std::string& error) {
	if (lua_type(L, index) != LUA_TSTRING) {
		error = std::string(label) + " must be a string";
		return false;
	}

	size_t len = 0;
	const char* value = lua_tolstring(L, index, &len);
	if (!value) {
		error = std::string(label) + " must be a string";
		return false;
	}

	*out = value;
	*length = len;
	return true;
}

bool ReadOptionalLuaStringArgument(lua_State* L,
								   int index,
								   const char* default_value,
								   const char* label,
								   const char** out,
								   size_t* length,
								   std::string& error) {
	if (lua_isnoneornil(L, index)) {
		*out = default_value;
		*length = std::strlen(default_value);
		return true;
	}
	return ReadLuaStringArgument(L, index, label, out, length, error);
}

bool ParseOid(std::string_view value, mongo::MongoOid* out, std::string& error) {
	mongo::MongoOid oid;
	if (value.size() != 24 || StringHasEmbeddedNull(value) ||
		!oid.IsValid(value.data(), value.size())) {
		error = "ObjectId must be a 24-character hex string";
		return false;
	}
	std::string oid_string(value);
	oid.InitFromString(oid_string.c_str());
	if (out) *out = oid;
	return true;
}

bool TryGetPositiveIntegerKey(lua_State* L, int index, size_t* out) {
	if (!lua_isinteger(L, index)) return false;

	int is_number = 0;
	lua_Integer key = lua_tointegerx(L, index, &is_number);
	if (!is_number || key < 1) return false;

	*out = static_cast<size_t>(key);
	return true;
}

LuaTableShape ClassifyLuaTableShape(lua_State* L, int table_index, size_t* length) {
	table_index = lua_absindex(L, table_index);

	size_t entry_count = 0;
	size_t positive_integer_count = 0;
	size_t max_index = 0;
	bool all_positive_integer_keys = true;

	lua_pushnil(L);
	while (lua_next(L, table_index) != 0) {
		if (IsInternalTableKey(L, -2)) {
			lua_pop(L, 1);
			continue;
		}

		++entry_count;
		size_t key = 0;
		const bool is_positive_integer_key = TryGetPositiveIntegerKey(L, -2, &key);
		lua_pop(L, 1);

		if (!is_positive_integer_key) {
			all_positive_integer_keys = false;
			continue;
		}

		++positive_integer_count;
		if (key > max_index) max_index = key;
	}

	if (length) *length = 0;
	if (entry_count == 0) {
		return LuaTableShape::Empty;
	}
	if (!all_positive_integer_keys) {
		return LuaTableShape::Document;
	}
	if (positive_integer_count != max_index) {
		return LuaTableShape::SparseArray;
	}
	if (length) *length = positive_integer_count;
	return LuaTableShape::DenseArray;
}

bool IsLuaArrayTable(lua_State* L, int table_index, size_t* length, bool allow_empty = false) {
	const LuaTableShape shape = ClassifyLuaTableShape(L, table_index, length);
	if (shape == LuaTableShape::DenseArray) return true;
	return allow_empty && shape == LuaTableShape::Empty;
}

bool LuaKeyToBsonKey(lua_State* L, int key_index, std::string& out, std::string& error) {
	const int type = lua_type(L, key_index);
	if (type == LUA_TSTRING) {
		size_t len = 0;
		const char* key = lua_tolstring(L, key_index, &len);
		if (!key) {
			error = "BSON key conversion failed";
			return false;
		}
		if (!EnsureValidBsonCString(std::string_view(key, len), "BSON document keys", error)) {
			return false;
		}
		out.assign(key, len);
		return true;
	}

	if (type == LUA_TNUMBER) {
		int is_number = 0;
		lua_Integer key = lua_tointegerx(L, key_index, &is_number);
		if (!is_number) {
			error = "BSON numeric table keys must be integers";
			return false;
		}
		out = std::to_string(static_cast<int64_t>(key));
		return true;
	}

	error = "BSON document keys must be strings or integers";
	return false;
}

bool AppendLuaValue(lua_State* L,
					int value_index,
					mongo::BsonDocument& parent,
					const char* key,
					int depth,
					int max_depth,
					int lua_stack_reserve,
					std::unordered_set<const void*>& active_tables,
					std::string& error);

bool IsBsonArrayDocument(const bson_t* doc);
bool ValidateBsonDocumentForCodec(const bson_t* doc,
								  bool as_array,
								  int depth,
								  int max_depth,
								  std::string& error);

void SetBsonUserdataRootArrayHint(lua_State* L, int userdata_index, bool root_as_array) {
	userdata_index = lua_absindex(L, userdata_index);
	lua_pushboolean(L, root_as_array ? 1 : 0);
	(void)lua_setuservalue(L, userdata_index);
}

bool GetBsonUserdataRootArrayHint(lua_State* L, int userdata_index, bool* root_as_array) {
	userdata_index = lua_absindex(L, userdata_index);
	lua_getuservalue(L, userdata_index);
	if (lua_isboolean(L, -1)) {
		*root_as_array = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		return true;
	}

	lua_pop(L, 1);
	return false;
}

bool ResolveBsonUserdataAsArray(lua_State* L, int userdata_index, const bson_t* raw) {
	bool root_as_array = false;
	if (GetBsonUserdataRootArrayHint(L, userdata_index, &root_as_array)) {
		return root_as_array;
	}
	return IsBsonArrayDocument(raw);
}

bool HasPublicTableFields(lua_State* L, int table_index) {
	table_index = lua_absindex(L, table_index);
	lua_pushnil(L);
	while (lua_next(L, table_index) != 0) {
		const bool is_public_key = !IsInternalTableKey(L, -2);
		lua_pop(L, 1);
		if (is_public_key) {
			lua_pop(L, 1);
			return true;
		}
	}
	return false;
}

WrapperBsonDocumentResult CopyWrapperBsonDocumentIfPresent(lua_State* L,
														   int table_index,
														   bool as_array,
														   int depth,
														   int max_depth,
														   mongo::BsonDocument& out,
														   std::string& error) {
	mongo::BsonDocument* wrapper_doc = nullptr;
	bool found = false;
	if (!GetRawBsonDocumentValue(L, table_index, &wrapper_doc, &found, error)) {
		return WrapperBsonDocumentResult::Error;
	}
	if (!found) return WrapperBsonDocumentResult::NotPresent;

	if (HasPublicTableFields(L, table_index)) {
		error = "BSON document wrapper must not contain Lua fields";
		return WrapperBsonDocumentResult::Error;
	}

	const auto* raw = static_cast<const bson_t*>(wrapper_doc->RawBson());
	if (!ValidateBsonForLuaConversion(raw, error) ||
		!ValidateBsonDocumentForCodec(raw, as_array, depth, max_depth, error)) {
		return WrapperBsonDocumentResult::Error;
	}
	if (!wrapper_doc->CopyTo(out)) {
		error = "failed to copy BSON document wrapper";
		return WrapperBsonDocumentResult::Error;
	}
	return WrapperBsonDocumentResult::Copied;
}

bool ValidateLuaValueDepthForBson(lua_State* L,
								  int value_index,
								  int depth,
								  int max_depth,
								  int lua_stack_reserve,
								  std::unordered_set<const void*>& active_tables,
								  std::string& error);

bool ValidateLuaTableDepthForBson(lua_State* L,
								  int table_index,
								  int depth,
								  int max_depth,
								  int lua_stack_reserve,
								  std::unordered_set<const void*>& active_tables,
								  std::string& error) {
	if (depth >= max_depth) {
		error = "Lua table nesting is too deep for BSON conversion";
		return false;
	}
	if (!EnsureLuaStack(L, lua_stack_reserve, error)) return false;

	table_index = lua_absindex(L, table_index);
	const void* table_pointer = lua_topointer(L, table_index);
	if (!table_pointer) {
		error = "Lua table conversion failed";
		return false;
	}

	ActiveTableGuard active_guard(&active_tables, table_pointer);
	active_guard.inserted = active_tables.insert(table_pointer).second;
	if (!active_guard.inserted) {
		error = "Lua table cycle detected during BSON conversion";
		return false;
	}

	const WrapperType wrapper_type = GetWrapperType(L, table_index);
	if (wrapper_type == WrapperType::Code) {
		lua_getfield(L, table_index, "scope");
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_rawgetp(L, table_index, &kScopeKey);
		}

		const bool ok = ValidateLuaValueDepthForBson(
			L, -1, depth + 1, max_depth, lua_stack_reserve, active_tables, error);
		lua_pop(L, 1);
		return ok;
	}

	if (wrapper_type != WrapperType::None && wrapper_type != WrapperType::Array &&
		wrapper_type != WrapperType::Document) {
		return true;
	}

	lua_pushnil(L);
	while (lua_next(L, table_index) != 0) {
		if (IsInternalTableKey(L, -2)) {
			lua_pop(L, 1);
			continue;
		}

		if (!ValidateLuaValueDepthForBson(
				L, -1, depth + 1, max_depth, lua_stack_reserve, active_tables, error)) {
			lua_pop(L, 1);
			lua_pop(L, 1);
			return false;
		}
		lua_pop(L, 1);
	}

	return true;
}

bool ValidateLuaValueDepthForBson(lua_State* L,
								  int value_index,
								  int depth,
								  int max_depth,
								  int lua_stack_reserve,
								  std::unordered_set<const void*>& active_tables,
								  std::string& error) {
	value_index = lua_absindex(L, value_index);

	if (lua_istable(L, value_index)) {
		return ValidateLuaTableDepthForBson(
			L, value_index, depth, max_depth, lua_stack_reserve, active_tables, error);
	}

	if (luaL_testudata(L, value_index, kBsonDocMetaName) && depth >= max_depth) {
		error = "BSON document nesting is too deep for Lua conversion";
		return false;
	}

	return true;
}

bool BuildBsonFromLuaTable(lua_State* L,
						   int table_index,
						   mongo::BsonDocument& out,
						   bool as_array,
						   int depth,
						   int max_depth,
						   int lua_stack_reserve,
						   std::unordered_set<const void*>& active_tables,
						   std::string& error) {
	if (depth >= max_depth) {
		error = "Lua table nesting is too deep for BSON conversion";
		return false;
	}
	if (!EnsureLuaStack(L, lua_stack_reserve, error)) return false;

	table_index = lua_absindex(L, table_index);
	const void* table_pointer = lua_topointer(L, table_index);
	if (!table_pointer) {
		error = "Lua table conversion failed";
		return false;
	}
	ActiveTableGuard active_guard(&active_tables, table_pointer);
	active_guard.inserted = active_tables.insert(table_pointer).second;
	if (!active_guard.inserted) {
		error = "Lua table cycle detected during BSON conversion";
		return false;
	}

	const WrapperType wrapper_type = GetWrapperType(L, table_index);
	if (wrapper_type == WrapperType::Array || wrapper_type == WrapperType::Document) {
		const WrapperBsonDocumentResult wrapper_doc_result =
			CopyWrapperBsonDocumentIfPresent(
				L, table_index, as_array, depth, max_depth, out, error);
		if (wrapper_doc_result == WrapperBsonDocumentResult::Error) return false;
		if (wrapper_doc_result == WrapperBsonDocumentResult::Copied) return true;
	}

	if (as_array) {
		size_t length = 0;
		if (!IsLuaArrayTable(L, table_index, &length, true)) {
			error = "Lua table is not a dense 1-based array";
			return false;
		}

		for (size_t i = 1; i <= length; ++i) {
			lua_rawgeti(L, table_index, static_cast<lua_Integer>(i));
			const std::string key = std::to_string(i - 1);
			const bool ok =
				AppendLuaValue(
					L, -1, out, key.c_str(), depth, max_depth, lua_stack_reserve, active_tables, error);
			lua_pop(L, 1);
			if (!ok) return false;
		}
		return true;
	}

	std::unordered_set<std::string> seen_keys;

	lua_pushnil(L);
	while (lua_next(L, table_index) != 0) {
		if (IsInternalTableKey(L, -2)) {
			lua_pop(L, 1);
			continue;
		}

		std::string key;
		if (!LuaKeyToBsonKey(L, -2, key, error)) {
			lua_pop(L, 1);
			lua_pop(L, 1);
			return false;
		}

		if (!seen_keys.insert(key).second) {
			error = "BSON document keys must be unique after conversion";
			lua_pop(L, 1);
			lua_pop(L, 1);
			return false;
		}

		if (!AppendLuaValue(
				L, -1, out, key.c_str(), depth, max_depth, lua_stack_reserve, active_tables, error)) {
			lua_pop(L, 1);
			lua_pop(L, 1);
			return false;
		}
		lua_pop(L, 1);
	}

	return true;
}

bool AppendLuaWrapper(lua_State* L,
					  int table_index,
					  mongo::BsonDocument& parent,
					  const char* key,
					  int depth,
					  int max_depth,
					  int lua_stack_reserve,
					  std::unordered_set<const void*>& active_tables,
					  std::string& error) {
	if (!EnsureLuaStack(L, lua_stack_reserve, error)) return false;

	table_index = lua_absindex(L, table_index);
	const WrapperType type = GetWrapperType(L, table_index);

	switch (type) {
	case WrapperType::Array:
	case WrapperType::Document: {
		if (depth + 1 >= max_depth) {
			error = "Lua table nesting is too deep for BSON conversion";
			return false;
		}

		mongo::BsonDocument child;
		const bool as_array = type == WrapperType::Array;
		if (!BuildBsonFromLuaTable(
				L,
				table_index,
				child,
				as_array,
				depth + 1,
				max_depth,
				lua_stack_reserve,
				active_tables,
				error)) {
			return false;
		}

		const bool ok =
			as_array ? parent.AppendArray(key, child) : parent.AppendDocument(key, child);
		if (!ok) error = "failed to append tagged Lua table to BSON document";
		return ok;
	}

	case WrapperType::Oid: {
		std::string value;
		mongo::MongoOid oid;
		if (!GetWrapperStringField(L, table_index, "value", &kValueKey, &value, error, "ObjectId") ||
			!ParseOid(value, &oid, error)) {
			return false;
		}
		if (parent.AppendOid(key, oid)) return true;
		error = "failed to append BSON ObjectId";
		return false;
	}

	case WrapperType::DateTime: {
		int64_t value = 0;
		if (!GetWrapperIntegerField(
				L, table_index, "value", &kValueKey, &value, error, "datetime value")) {
			return false;
		}
		if (parent.AppendDateTime(key, value)) return true;
		error = "failed to append BSON datetime";
		return false;
	}

	case WrapperType::Timestamp: {
		int64_t timestamp_value = 0;
		int64_t increment_value = 0;
		uint32_t timestamp = 0;
		uint32_t increment = 0;
		if (!GetWrapperIntegerField(L,
									table_index,
									"timestamp",
									&kValueKey,
									&timestamp_value,
									error,
									"timestamp") ||
			!GetWrapperIntegerField(L,
									table_index,
									"increment",
									&kIncrementKey,
									&increment_value,
									error,
									"timestamp increment") ||
			!ValidateUInt32(timestamp_value, "timestamp", &timestamp, error) ||
			!ValidateUInt32(increment_value, "timestamp increment", &increment, error)) {
			return false;
		}
		if (parent.AppendTimestamp(key, timestamp, increment)) return true;
		error = "failed to append BSON timestamp";
		return false;
	}

	case WrapperType::Binary: {
		std::string value;
		int64_t subtype_value = 0;
		uint8_t subtype = 0;
		if (!GetWrapperStringField(
				L, table_index, "value", &kValueKey, &value, error, "binary value") ||
			!GetWrapperIntegerField(L,
									table_index,
									"subtype",
									&kSubtypeKey,
									&subtype_value,
									error,
									"binary subtype") ||
			!ValidateByte(subtype_value, "binary subtype", &subtype, error)) {
			return false;
		}
		if (value.size() > std::numeric_limits<uint32_t>::max()) {
			error = "binary value is too large for BSON";
			return false;
		}
		if (parent.AppendBinary(key,
								static_cast<int>(subtype),
								reinterpret_cast<const uint8_t*>(value.data()),
								static_cast<uint32_t>(value.size()))) {
			return true;
		}
		error = "failed to append BSON binary";
		return false;
	}

	case WrapperType::Regex: {
		std::string pattern;
		std::string options;
		if (!GetWrapperStringField(
				L, table_index, "pattern", &kValueKey, &pattern, error, "regex pattern") ||
			!GetWrapperStringField(
				L, table_index, "options", &kOptionsKey, &options, error, "regex options") ||
			!EnsureValidBsonCString(pattern, "regex pattern", error) ||
			!EnsureValidBsonCString(options, "regex options", error) ||
			!EnsureValidRegexOptions(options, error)) {
			return false;
		}
		if (parent.AppendRegex(key, pattern.c_str(), options.c_str())) return true;
		error = "failed to append BSON regex";
		return false;
	}

	case WrapperType::Code: {
		std::string value;
		if (!GetWrapperStringField(
				L, table_index, "value", &kValueKey, &value, error, "code value") ||
			!EnsureValidBsonCString(value, "code value", error)) {
			return false;
		}

		lua_getfield(L, table_index, "scope");
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_rawgetp(L, table_index, &kScopeKey);
		}

		const int scope_type = lua_type(L, -1);
		if (scope_type == LUA_TNIL) {
			lua_pop(L, 1);
			if (parent.AppendCode(key, value.c_str())) return true;
			error = "failed to append BSON code";
			return false;
		}

		mongo::BsonDocument scope_doc;
		if (scope_type == LUA_TTABLE) {
			const WrapperType scope_wrapper = GetWrapperType(L, -1);
			if (scope_wrapper != WrapperType::None && scope_wrapper != WrapperType::Document) {
				lua_pop(L, 1);
				error = "code scope must be a document table";
				return false;
			}
			if (!BuildBsonFromLuaTable(
					L,
					-1,
					scope_doc,
					false,
					depth + 1,
					max_depth,
					lua_stack_reserve,
					active_tables,
					error)) {
				lua_pop(L, 1);
				return false;
			}
		} else {
			auto** doc =
				static_cast<mongo::BsonDocument**>(luaL_testudata(L, -1, kBsonDocMetaName));
			if (!doc || !*doc) {
				lua_pop(L, 1);
				error = "code scope must be a document table or bson.doc";
				return false;
			}
			const auto* raw = static_cast<const bson_t*>((*doc)->RawBson());
			if (!ValidateBsonForLuaConversion(raw, error)) {
				lua_pop(L, 1);
				return false;
			}
			if (ResolveBsonUserdataAsArray(L, -1, raw)) {
				lua_pop(L, 1);
				error = "code scope must be a document table or bson.doc";
				return false;
			}
			if (!ValidateBsonDocumentForCodec(raw, false, depth + 1, max_depth, error)) {
				lua_pop(L, 1);
				return false;
			}
			if (!(*doc)->CopyTo(scope_doc)) {
				lua_pop(L, 1);
				error = "failed to copy BSON code scope";
				return false;
			}
		}

		lua_pop(L, 1);
		if (parent.AppendCodeWithScope(key, value.c_str(), scope_doc)) return true;
		error = "failed to append BSON code with scope";
		return false;
	}

	case WrapperType::Symbol: {
		std::string value;
		if (!GetWrapperStringField(
				L, table_index, "value", &kValueKey, &value, error, "symbol value") ||
			!EnsureValidBsonText(value, "symbol value", error)) {
			return false;
		}
		if (AppendBsonSymbol(parent, key, value, error)) return true;
		if (error.empty()) error = "failed to append BSON symbol";
		return false;
	}

	case WrapperType::Decimal128: {
		std::string value;
		if (!GetWrapperStringField(
				L, table_index, "value", &kValueKey, &value, error, "decimal128 value") ||
			!EnsureNoEmbeddedNull(value, "decimal128 value", error)) {
			return false;
		}

		mongo::MongoDecimal128 decimal;
		if (!decimal.FromString(value.c_str())) {
			error = "decimal128 value is invalid";
			return false;
		}
		if (parent.AppendDecimal128(key, decimal)) return true;
		error = "failed to append BSON decimal128";
		return false;
	}

	case WrapperType::DBPointer: {
		std::string collection;
		std::string oid_value;
		mongo::MongoOid oid;
		if (!GetWrapperStringField(L,
								   table_index,
								   "collection",
								   &kValueKey,
								   &collection,
								   error,
								   "dbpointer collection") ||
			!GetWrapperStringField(
				L, table_index, "oid", &kOidKey, &oid_value, error, "dbpointer oid") ||
			!EnsureValidBsonCString(collection, "dbpointer collection", error) ||
			!ParseOid(oid_value, &oid, error)) {
			return false;
		}
		if (parent.AppendDBPointer(key, collection.c_str(), oid)) return true;
		error = "failed to append BSON dbpointer";
		return false;
	}

	case WrapperType::Int32: {
		int64_t value = 0;
		int32_t int32_value = 0;
		if (!GetWrapperIntegerField(
				L, table_index, "value", &kValueKey, &value, error, "int32 value") ||
			!ValidateInt32(value, "int32 value", &int32_value, error)) {
			return false;
		}
		if (parent.AppendInt32(key, int32_value)) return true;
		error = "failed to append BSON int32";
		return false;
	}

	case WrapperType::Int64: {
		int64_t value = 0;
		if (!GetWrapperIntegerField(
				L, table_index, "value", &kValueKey, &value, error, "int64 value")) {
			return false;
		}
		if (parent.AppendInt64(key, value)) return true;
		error = "failed to append BSON int64";
		return false;
	}

	case WrapperType::Double: {
		double value = 0.0;
		if (!GetWrapperNumberField(
				L, table_index, "value", &kValueKey, &value, error, "double value")) {
			return false;
		}
		if (parent.AppendDouble(key, value)) return true;
		error = "failed to append BSON double";
		return false;
	}

	default:
		error = "unknown BSON Lua wrapper type";
		return false;
	}
}

bool AppendLuaTable(lua_State* L,
					int table_index,
					mongo::BsonDocument& parent,
					const char* key,
					int depth,
					int max_depth,
					int lua_stack_reserve,
					std::unordered_set<const void*>& active_tables,
					std::string& error) {
	if (!EnsureLuaStack(L, lua_stack_reserve, error)) return false;
	if (depth + 1 >= max_depth) {
		error = "Lua table nesting is too deep for BSON conversion";
		return false;
	}

	size_t array_length = 0;
	const LuaTableShape shape = ClassifyLuaTableShape(L, table_index, &array_length);
	if (shape == LuaTableShape::SparseArray) {
		error = kSparseArrayError;
		return false;
	}
	const bool is_array = shape == LuaTableShape::DenseArray;

	mongo::BsonDocument child;
	if (!BuildBsonFromLuaTable(
			L,
			table_index,
			child,
			is_array,
			depth + 1,
			max_depth,
			lua_stack_reserve,
			active_tables,
			error)) {
		return false;
	}

	const bool ok = is_array ? parent.AppendArray(key, child) : parent.AppendDocument(key, child);
	if (!ok) error = "failed to append Lua table to BSON document";
	return ok;
}

bool AppendLuaValue(lua_State* L,
					int value_index,
					mongo::BsonDocument& parent,
					const char* key,
					int depth,
					int max_depth,
					int lua_stack_reserve,
					std::unordered_set<const void*>& active_tables,
					std::string& error) {
	value_index = lua_absindex(L, value_index);

	switch (lua_type(L, value_index)) {
	case LUA_TBOOLEAN:
		if (parent.AppendBool(key, lua_toboolean(L, value_index) != 0)) return true;
		error = "failed to append BSON bool";
		return false;

	case LUA_TNUMBER: {
		if (lua_isinteger(L, value_index)) {
			const auto value = static_cast<int64_t>(lua_tointeger(L, value_index));
			const bool fits_int32 = value >= std::numeric_limits<int32_t>::min() &&
									value <= std::numeric_limits<int32_t>::max();
			const bool ok = fits_int32 ? parent.AppendInt32(key, static_cast<int32_t>(value))
									   : parent.AppendInt64(key, value);
			if (ok) return true;
			error = "failed to append BSON integer";
			return false;
		}

		const double value = static_cast<double>(lua_tonumber(L, value_index));
		if (parent.AppendDouble(key, value)) return true;
		error = "failed to append BSON double";
		return false;
	}

	case LUA_TSTRING: {
		size_t len = 0;
		const char* value = lua_tolstring(L, value_index, &len);
		const std::string_view text(value ? value : "", value ? len : 0);
		if (!EnsureValidBsonText(text,
								 "BSON UTF-8 string",
								 error)) {
			return false;
		}
		if (parent.AppendUtf8(key, text)) return true;
		error = "failed to append BSON UTF-8 string";
		return false;
	}

	case LUA_TTABLE:
		if (GetWrapperType(L, value_index) != WrapperType::None) {
			return AppendLuaWrapper(
				L, value_index, parent, key, depth, max_depth, lua_stack_reserve, active_tables, error);
		}
		return AppendLuaTable(
			L, value_index, parent, key, depth, max_depth, lua_stack_reserve, active_tables, error);

	case LUA_TLIGHTUSERDATA:
		if (IsNullSentinel(L, value_index)) {
			if (parent.AppendNull(key)) return true;
			error = "failed to append BSON null";
			return false;
		}
		if (IsUndefinedSentinel(L, value_index)) {
			if (parent.AppendUndefined(key)) return true;
			error = "failed to append BSON undefined";
			return false;
		}
		if (IsMinKeySentinel(L, value_index)) {
			if (parent.AppendMinkey(key)) return true;
			error = "failed to append BSON minkey";
			return false;
		}
		if (IsMaxKeySentinel(L, value_index)) {
			if (parent.AppendMaxkey(key)) return true;
			error = "failed to append BSON maxkey";
			return false;
		}
		error = "unsupported lightuserdata in Lua table";
		return false;

	case LUA_TUSERDATA: {
		auto** doc =
			static_cast<mongo::BsonDocument**>(luaL_testudata(L, value_index, kBsonDocMetaName));
		if (doc && *doc) {
			const auto* raw = static_cast<const bson_t*>((*doc)->RawBson());
			if (!ValidateBsonForLuaConversion(raw, error)) {
				return false;
			}
			const bool as_array = ResolveBsonUserdataAsArray(L, value_index, raw);
			if (!ValidateBsonDocumentForCodec(raw, as_array, depth + 1, max_depth, error)) {
				return false;
			}
			const bool ok = as_array ? parent.AppendArray(key, **doc)
									 : parent.AppendDocument(key, **doc);
			if (ok) return true;
			error = as_array ? "failed to append nested BSON array"
							 : "failed to append nested BSON document";
			return false;
		}
		error = "unsupported userdata in Lua table";
		return false;
	}

	default:
		error = std::string("unsupported Lua value type: ") + luaL_typename(L, value_index);
		return false;
	}
}

bool ParseCanonicalBsonArrayIndex(const char* key, uint64_t* zero_based) {
	const size_t len = std::strlen(key);
	if (len == 0) return false;
	if (key[0] < '0' || key[0] > '9') return false;
	if (len > 1 && key[0] == '0') return false;

	uint64_t parsed = 0;
	const char* end = key + len;
	const auto result = std::from_chars(key, end, parsed);
	if (result.ec != std::errc{} || result.ptr != end) return false;
	if (parsed >= static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max())) {
		return false;
	}

	*zero_based = parsed;
	return true;
}

bool IsExpectedBsonArrayKey(const char* key, uint64_t expected_zero_based) {
	uint64_t zero_based = 0;
	return ParseCanonicalBsonArrayIndex(key, &zero_based) && zero_based == expected_zero_based;
}

bool IsBsonArrayDocument(const bson_t* doc) {
	bson_iter_t iter;
	if (!bson_iter_init(&iter, doc)) return false;

	uint64_t expected_index = 0;
	while (bson_iter_next(&iter)) {
		if (!IsExpectedBsonArrayKey(bson_iter_key(&iter), expected_index)) return false;
		++expected_index;
	}

	return expected_index > 0;
}

bool ValidateBsonIterValueForCodec(const bson_iter_t* iter,
								   int depth,
								   int max_depth,
								   std::string& error);

bool ValidateBsonDocumentForCodec(const bson_t* doc,
								  bool as_array,
								  int depth,
								  int max_depth,
								  std::string& error) {
	if (depth >= max_depth) {
		error = "BSON document nesting is too deep for Lua conversion";
		return false;
	}

	bson_iter_t iter;
	if (!bson_iter_init(&iter, doc)) {
		error = "failed to iterate BSON document";
		return false;
	}

	uint64_t expected_array_index = 0;
	std::unordered_set<std::string> seen_keys;
	while (bson_iter_next(&iter)) {
		const char* key = bson_iter_key(&iter);
		if (as_array) {
			if (!IsExpectedBsonArrayKey(key, expected_array_index)) {
				error = "BSON array keys must be dense zero-based indexes";
				return false;
			}
			++expected_array_index;
		} else if (!seen_keys.insert(key ? key : "").second) {
			error = "BSON document keys must be unique";
			return false;
		}

		if (!ValidateBsonIterValueForCodec(&iter, depth, max_depth, error)) {
			return false;
		}
	}

	return true;
}

bool ValidateBsonIterValueForCodec(const bson_iter_t* iter,
								   int depth,
								   int max_depth,
								   std::string& error) {
	if (depth >= max_depth) {
		error = "BSON document nesting is too deep for Lua conversion";
		return false;
	}

	switch (bson_iter_type(iter)) {
	case BSON_TYPE_DOUBLE:
	case BSON_TYPE_BINARY:
	case BSON_TYPE_UNDEFINED:
	case BSON_TYPE_NULL:
	case BSON_TYPE_OID:
	case BSON_TYPE_BOOL:
	case BSON_TYPE_DATE_TIME:
	case BSON_TYPE_INT32:
	case BSON_TYPE_TIMESTAMP:
	case BSON_TYPE_INT64:
	case BSON_TYPE_DECIMAL128:
	case BSON_TYPE_MAXKEY:
	case BSON_TYPE_MINKEY:
		return true;

	case BSON_TYPE_UTF8: {
		uint32_t len = 0;
		const char* value = bson_iter_utf8(iter, &len);
		return EnsureValidBsonText(
			std::string_view(value ? value : "", value ? len : 0),
			"BSON UTF-8 string",
			error);
	}

	case BSON_TYPE_DOCUMENT:
	case BSON_TYPE_ARRAY: {
		uint32_t len = 0;
		const uint8_t* data = nullptr;
		const bool value_is_array = bson_iter_type(iter) == BSON_TYPE_ARRAY;
		if (value_is_array) {
			bson_iter_array(iter, &len, &data);
		} else {
			bson_iter_document(iter, &len, &data);
		}

		bson_t child;
		if (!bson_init_static(&child, data, len)) {
			error = value_is_array ? "failed to read BSON array"
								   : "failed to read nested BSON document";
			return false;
		}
		const bool ok =
			ValidateBsonDocumentForCodec(&child, value_is_array, depth + 1, max_depth, error);
		bson_destroy(&child);
		return ok;
	}

	case BSON_TYPE_REGEX: {
		const char* options = nullptr;
		const char* regex = bson_iter_regex(iter, &options);
		const std::string_view regex_view(regex ? regex : "");
		const std::string_view options_view(options ? options : "");
		return EnsureValidBsonCString(regex_view, "regex pattern", error) &&
			   EnsureValidBsonCString(options_view, "regex options", error) &&
			   EnsureValidRegexOptions(options_view, error);
	}

	case BSON_TYPE_DBPOINTER: {
		uint32_t collection_len = 0;
		const char* collection = nullptr;
		const bson_oid_t* oid = nullptr;
		bson_iter_dbpointer(iter, &collection_len, &collection, &oid);
		if (!collection || !oid) {
			error = "invalid BSON DBPointer";
			return false;
		}
		return EnsureValidBsonCString(
			std::string_view(collection, collection_len), "dbpointer collection", error);
	}

	case BSON_TYPE_CODE: {
		uint32_t len = 0;
		const char* code = bson_iter_code(iter, &len);
		return EnsureValidBsonCString(
			std::string_view(code ? code : "", code ? len : 0), "code value", error);
	}

	case BSON_TYPE_SYMBOL: {
		uint32_t len = 0;
		const char* symbol = bson_iter_symbol(iter, &len);
		return EnsureValidBsonText(
			std::string_view(symbol ? symbol : "", symbol ? len : 0),
			"symbol value",
			error);
	}

	case BSON_TYPE_CODEWSCOPE: {
		uint32_t code_len = 0;
		uint32_t scope_len = 0;
		const uint8_t* scope_data = nullptr;
		const char* code = bson_iter_codewscope(iter, &code_len, &scope_len, &scope_data);
		if (!EnsureValidBsonCString(
				std::string_view(code ? code : "", code ? code_len : 0),
				"code value",
				error)) {
			return false;
		}
		if (!scope_data) {
			error = "failed to read BSON code scope";
			return false;
		}

		bson_t scope;
		if (!bson_init_static(&scope, scope_data, scope_len)) {
			error = "failed to read BSON code scope";
			return false;
		}
		const bool ok =
			ValidateBsonDocumentForCodec(&scope, false, depth + 1, max_depth, error);
		bson_destroy(&scope);
		return ok;
	}

	default:
		error = "unsupported BSON value type";
		return false;
	}
}

bool PushBsonDocumentAsTable(lua_State* L,
							 const bson_t* doc,
							 bool as_array,
							 bool preserve_types,
							 int depth,
							 int max_depth,
							 int lua_stack_reserve,
							 std::string& error);

bool PushBsonIterValue(lua_State* L,
					   const bson_iter_t* iter,
					   bool preserve_types,
					   int depth,
					   int max_depth,
					   int lua_stack_reserve,
					   std::string& error) {
	if (depth >= max_depth) {
		error = "BSON document nesting is too deep for Lua conversion";
		return false;
	}

	switch (bson_iter_type(iter)) {
	case BSON_TYPE_DOUBLE:
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Double);
			SetWrapperNumberField(L, -1, "value", &kValueKey, bson_iter_double(iter));
			return true;
		}
		lua_pushnumber(L, static_cast<lua_Number>(bson_iter_double(iter)));
		return true;

	case BSON_TYPE_UTF8: {
		uint32_t len = 0;
		const char* value = bson_iter_utf8(iter, &len);
		lua_pushlstring(L, value ? value : "", value ? len : 0);
		return true;
	}

	case BSON_TYPE_DOCUMENT: {
		uint32_t len = 0;
		const uint8_t* data = nullptr;
		bson_iter_document(iter, &len, &data);

		bson_t child;
		if (!bson_init_static(&child, data, len)) {
			error = "failed to read nested BSON document";
			return false;
		}
		const bool ok =
			PushBsonDocumentAsTable(
				L, &child, false, preserve_types, depth + 1, max_depth, lua_stack_reserve, error);
		bson_destroy(&child);
		return ok;
	}

	case BSON_TYPE_ARRAY: {
		uint32_t len = 0;
		const uint8_t* data = nullptr;
		bson_iter_array(iter, &len, &data);

		bson_t child;
		if (!bson_init_static(&child, data, len)) {
			error = "failed to read BSON array";
			return false;
		}
		const bool ok =
			PushBsonDocumentAsTable(
				L, &child, true, preserve_types, depth + 1, max_depth, lua_stack_reserve, error);
		bson_destroy(&child);
		return ok;
	}

	case BSON_TYPE_BINARY: {
		bson_subtype_t subtype;
		uint32_t len = 0;
		const uint8_t* data = nullptr;
		bson_iter_binary(iter, &subtype, &len, &data);
		if (preserve_types) {
			const auto* bytes = reinterpret_cast<const char*>(data);
			PushTaggedTable(L, WrapperType::Binary);
			SetWrapperStringField(L,
								  -1,
								  "value",
								  &kValueKey,
								  std::string_view(bytes ? bytes : "", bytes ? len : 0));
			SetWrapperIntegerField(L, -1, "subtype", &kSubtypeKey, static_cast<int64_t>(subtype));
			return true;
		}
		lua_pushlstring(L, data ? reinterpret_cast<const char*>(data) : "", data ? len : 0);
		return true;
	}

	case BSON_TYPE_UNDEFINED:
		PushUndefinedSentinel(L);
		return true;

	case BSON_TYPE_NULL:
		PushNullSentinel(L);
		return true;

	case BSON_TYPE_OID: {
		char oid[25];
		bson_oid_to_string(bson_iter_oid(iter), oid);
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Oid);
			SetWrapperStringField(L, -1, "value", &kValueKey, oid);
			return true;
		}
		lua_pushstring(L, oid);
		return true;
	}

	case BSON_TYPE_BOOL:
		lua_pushboolean(L, bson_iter_bool(iter) ? 1 : 0);
		return true;

	case BSON_TYPE_DATE_TIME:
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::DateTime);
			SetWrapperIntegerField(L, -1, "value", &kValueKey, bson_iter_date_time(iter));
			return true;
		}
		return PushInteger64(L, bson_iter_date_time(iter));

	case BSON_TYPE_REGEX: {
		const char* options = nullptr;
		const char* regex = bson_iter_regex(iter, &options);
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Regex);
			SetWrapperStringField(L, -1, "pattern", &kValueKey, regex ? regex : "");
			SetWrapperStringField(L, -1, "options", &kOptionsKey, options ? options : "");
			return true;
		}
		lua_newtable(L);
		SetStringField(L, "$regex", regex);
		SetStringField(L, "$options", options);
		return true;
	}

	case BSON_TYPE_DBPOINTER: {
		uint32_t collection_len = 0;
		const char* collection = nullptr;
		const bson_oid_t* oid = nullptr;
		bson_iter_dbpointer(iter, &collection_len, &collection, &oid);

		char oid_string[25] = {};
		if (oid) bson_oid_to_string(oid, oid_string);

		if (preserve_types) {
			PushTaggedTable(L, WrapperType::DBPointer);
			SetWrapperStringField(L,
								  -1,
								  "collection",
								  &kValueKey,
								  std::string_view(collection ? collection : "",
												   collection ? collection_len : 0));
			SetWrapperStringField(L, -1, "oid", &kOidKey, oid_string);
			return true;
		}

		lua_newtable(L);
		lua_pushlstring(L, collection ? collection : "", collection ? collection_len : 0);
		lua_setfield(L, -2, "$ref");
		SetStringField(L, "$id", oid_string);
		return true;
	}

	case BSON_TYPE_CODE: {
		uint32_t len = 0;
		const char* code = bson_iter_code(iter, &len);
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Code);
			SetWrapperStringField(
				L, -1, "value", &kValueKey, std::string_view(code ? code : "", code ? len : 0));
			return true;
		}
		lua_pushlstring(L, code ? code : "", code ? len : 0);
		return true;
	}

	case BSON_TYPE_SYMBOL: {
		uint32_t len = 0;
		const char* symbol = bson_iter_symbol(iter, &len);
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Symbol);
			SetWrapperStringField(L,
								  -1,
								  "value",
								  &kValueKey,
								  std::string_view(symbol ? symbol : "", symbol ? len : 0));
			return true;
		}
		lua_pushlstring(L, symbol ? symbol : "", symbol ? len : 0);
		return true;
	}

	case BSON_TYPE_CODEWSCOPE: {
		uint32_t code_len = 0;
		uint32_t scope_len = 0;
		const uint8_t* scope_data = nullptr;
		const char* code = bson_iter_codewscope(iter, &code_len, &scope_len, &scope_data);

		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Code);
			const int wrapper_index = lua_gettop(L);
			SetWrapperStringField(L,
								  wrapper_index,
								  "value",
								  &kValueKey,
								  std::string_view(code ? code : "", code ? code_len : 0));
			if (scope_data) {
				bson_t scope;
				if (!bson_init_static(&scope, scope_data, scope_len)) {
					lua_pop(L, 1);
					error = "failed to read BSON code scope";
					return false;
				}
				const bool ok =
					PushBsonDocumentAsTable(
						L,
						&scope,
						false,
						preserve_types,
						depth + 1,
						max_depth,
						lua_stack_reserve,
						error);
				bson_destroy(&scope);
				if (!ok) {
					lua_pop(L, 1);
					return false;
				}
				lua_pushvalue(L, -1);
				lua_rawsetp(L, wrapper_index, &kScopeKey);
				lua_setfield(L, wrapper_index, "scope");
			}
			return true;
		}

		lua_newtable(L);
		lua_pushlstring(L, code ? code : "", code ? code_len : 0);
		lua_setfield(L, -2, "$code");

		if (scope_data) {
			bson_t scope;
			if (!bson_init_static(&scope, scope_data, scope_len)) {
				lua_pop(L, 1);
				error = "failed to read BSON code scope";
				return false;
			}
			const bool ok =
				PushBsonDocumentAsTable(
					L,
					&scope,
					false,
					preserve_types,
					depth + 1,
					max_depth,
					lua_stack_reserve,
					error);
			bson_destroy(&scope);
			if (!ok) {
				lua_pop(L, 1);
				return false;
			}
			lua_setfield(L, -2, "$scope");
		}
		return true;
	}

	case BSON_TYPE_INT32:
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Int32);
			SetWrapperIntegerField(L, -1, "value", &kValueKey, bson_iter_int32(iter));
			return true;
		}
		lua_pushinteger(L, static_cast<lua_Integer>(bson_iter_int32(iter)));
		return true;

	case BSON_TYPE_TIMESTAMP: {
		uint32_t timestamp = 0;
		uint32_t increment = 0;
		bson_iter_timestamp(iter, &timestamp, &increment);

		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Timestamp);
			SetWrapperIntegerField(L, -1, "timestamp", &kValueKey, timestamp);
			SetWrapperIntegerField(L, -1, "increment", &kIncrementKey, increment);
			return true;
		}

		lua_newtable(L);
		SetIntegerField(L, "timestamp", timestamp);
		SetIntegerField(L, "increment", increment);
		return true;
	}

	case BSON_TYPE_INT64:
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Int64);
			SetWrapperIntegerField(L, -1, "value", &kValueKey, bson_iter_int64(iter));
			return true;
		}
		return PushInteger64(L, bson_iter_int64(iter));

	case BSON_TYPE_DECIMAL128: {
		bson_decimal128_t decimal;
		if (!bson_iter_decimal128(iter, &decimal)) {
			error = "failed to read BSON decimal128";
			return false;
		}

		char decimal_string[BSON_DECIMAL128_STRING];
		bson_decimal128_to_string(&decimal, decimal_string);
		if (preserve_types) {
			PushTaggedTable(L, WrapperType::Decimal128);
			SetWrapperStringField(L, -1, "value", &kValueKey, decimal_string);
			return true;
		}
		lua_pushstring(L, decimal_string);
		return true;
	}

	case BSON_TYPE_MAXKEY:
		PushMaxKeySentinel(L);
		return true;

	case BSON_TYPE_MINKEY:
		PushMinKeySentinel(L);
		return true;

	default:
		error = "unsupported BSON value type";
		return false;
	}
}

bool PushBsonDocumentAsTable(lua_State* L,
							 const bson_t* doc,
							 bool as_array,
							 bool preserve_types,
							 int depth,
							 int max_depth,
							 int lua_stack_reserve,
							 std::string& error) {
	if (depth >= max_depth) {
		error = "BSON document nesting is too deep for Lua conversion";
		return false;
	}
	if (!EnsureLuaStack(L, lua_stack_reserve, error)) return false;

	bson_iter_t iter;
	if (!bson_iter_init(&iter, doc)) {
		error = "failed to iterate BSON document";
		return false;
	}

	lua_newtable(L);
	const int table_index = lua_gettop(L);
	if (preserve_types) {
		SetWrapperType(L, table_index, as_array ? WrapperType::Array : WrapperType::Document);
	}

	uint64_t expected_array_index = 0;
	std::unordered_set<std::string> seen_keys;
	while (bson_iter_next(&iter)) {
		const char* key = bson_iter_key(&iter);
		if (as_array &&
			!IsExpectedBsonArrayKey(key, expected_array_index)) {
			lua_pop(L, 1);
			error = "BSON array keys must be dense zero-based indexes";
			return false;
		}
		if (!as_array && !seen_keys.insert(key ? key : "").second) {
			lua_pop(L, 1);
			error = "BSON document keys must be unique";
			return false;
		}

		if (!PushBsonIterValue(
				L, &iter, preserve_types, depth, max_depth, lua_stack_reserve, error)) {
			lua_pop(L, 1);
			return false;
		}

		if (as_array) {
			lua_seti(L, table_index, static_cast<lua_Integer>(expected_array_index + 1));
			++expected_array_index;
		} else {
			lua_setfield(L, table_index, key);
		}
	}

	return true;
}

bool ReadOptionalBoolField(lua_State* L,
						   int table_index,
						   const char* field,
						   bool* value,
						   bool* is_set,
						   std::string& error) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, field);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return true;
	}
	if (!lua_isboolean(L, -1)) {
		lua_pop(L, 1);
		error = std::string("option '") + field + "' must be a boolean";
		return false;
	}
	*value = lua_toboolean(L, -1) != 0;
	*is_set = true;
	lua_pop(L, 1);
	return true;
}

bool ReadOptionalIntegerField(lua_State* L,
							  int table_index,
							  const char* field,
							  int64_t* value,
							  bool* is_set,
							  std::string& error) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, field);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return true;
	}
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		error = std::string("option '") + field + "' must be an integer";
		return false;
	}
	*value = static_cast<int64_t>(lua_tointeger(L, -1));
	*is_set = true;
	lua_pop(L, 1);
	return true;
}

bool ValidateIntegerOptionRange(const char* field,
								int64_t value,
								int min_value,
								int max_value,
								int* out,
								std::string& error) {
	if (value < min_value || value > max_value) {
		error = std::string("option '") + field + "' must be between 1 and " +
				std::to_string(max_value);
		return false;
	}
	*out = static_cast<int>(value);
	return true;
}

bool ValidateMaxDepthOption(const TableOptions& options,
							const char* field,
							int64_t value,
							int* out,
							std::string& error) {
	return ValidateIntegerOptionRange(
		field, value, 1, options.max_configurable_depth, out, error);
}

bool ParseOptions(lua_State* L,
				  int index,
				  bool allow_preserve_types,
				  TableOptions* options,
				  std::string& error) {
	if (lua_isnoneornil(L, index)) return true;
	if (lua_isboolean(L, index)) {
		options->root_as_array = lua_toboolean(L, index) != 0;
		options->root_as_array_set = true;
		return true;
	}
	if (!lua_istable(L, index)) {
		error = "options must be a boolean or table";
		return false;
	}

	index = lua_absindex(L, index);
	bool root_as_array = false;
	bool root_as_array_set = false;
	if (!ReadOptionalBoolField(
			L, index, "root_as_array", &root_as_array, &root_as_array_set, error)) {
		return false;
	}
	bool array_alias = false;
	bool array_alias_set = false;
	if (!ReadOptionalBoolField(L, index, "array", &array_alias, &array_alias_set, error)) {
		return false;
	}
	if (root_as_array_set && array_alias_set && root_as_array != array_alias) {
		error = "options 'root_as_array' and 'array' must match";
		return false;
	}
	if (root_as_array_set) {
		options->root_as_array = root_as_array;
		options->root_as_array_set = true;
	} else if (array_alias_set) {
		options->root_as_array = array_alias;
		options->root_as_array_set = true;
	}

	int64_t max_configurable_depth = 0;
	bool max_configurable_depth_set = false;
	if (!ReadOptionalIntegerField(L,
								  index,
								  "max_configurable_depth",
								  &max_configurable_depth,
								  &max_configurable_depth_set,
								  error)) {
		return false;
	}
	if (max_configurable_depth_set &&
		!ValidateIntegerOptionRange("max_configurable_depth",
									max_configurable_depth,
									1,
									kHardMaxConfigurableDepth,
									&options->max_configurable_depth,
									error)) {
		return false;
	}

	int64_t lua_stack_reserve = 0;
	bool lua_stack_reserve_set = false;
	if (!ReadOptionalIntegerField(
			L, index, "lua_stack_reserve", &lua_stack_reserve, &lua_stack_reserve_set, error)) {
		return false;
	}
	if (lua_stack_reserve_set &&
		!ValidateIntegerOptionRange("lua_stack_reserve",
									lua_stack_reserve,
									1,
									kHardMaxLuaStackReserve,
									&options->lua_stack_reserve,
									error)) {
		return false;
	}

	int64_t max_depth = 0;
	bool max_depth_set = false;
	if (!ReadOptionalIntegerField(L, index, "max_depth", &max_depth, &max_depth_set, error)) {
		return false;
	}
	int64_t max_nesting_depth = 0;
	bool max_nesting_depth_set = false;
	if (!ReadOptionalIntegerField(L,
								  index,
								  "max_nesting_depth",
								  &max_nesting_depth,
								  &max_nesting_depth_set,
								  error)) {
		return false;
	}
	if (max_depth_set && max_nesting_depth_set && max_depth != max_nesting_depth) {
		error = "options 'max_depth' and 'max_nesting_depth' must match";
		return false;
	}
	if (max_depth_set) {
		if (!ValidateMaxDepthOption(*options, "max_depth", max_depth, &options->max_depth, error)) {
			return false;
		}
	} else if (max_nesting_depth_set) {
		if (!ValidateMaxDepthOption(
				*options, "max_nesting_depth", max_nesting_depth, &options->max_depth, error)) {
			return false;
		}
	} else if (options->max_depth > options->max_configurable_depth) {
		options->max_depth = options->max_configurable_depth;
	}

	if (allow_preserve_types) {
		bool preserve_set = false;
		if (!ReadOptionalBoolField(
				L, index, "preserve_types", &options->preserve_types, &preserve_set, error)) {
			return false;
		}
	}

	return true;
}

bool ResolveRootTableOptions(lua_State* L,
							 int table_index,
							 int options_index,
							 const char* function_name,
							 TableOptions* options,
							 bool* root_as_array,
							 std::string& error) {
	if (!ParseOptions(L, options_index, false, options, error)) return false;

	table_index = lua_absindex(L, table_index);
	const WrapperType root_type = GetWrapperType(L, table_index);
	if (root_type == WrapperType::Array || root_type == WrapperType::Document) {
		options->root_as_array = root_type == WrapperType::Array;
		options->root_as_array_set = true;
	} else if (root_type != WrapperType::None) {
		error = std::string(function_name) + " root must be a document or array table";
		return false;
	}

	if (options->root_as_array_set) {
		*root_as_array = options->root_as_array;
		return true;
	}

	size_t ignored_length = 0;
	const LuaTableShape shape = ClassifyLuaTableShape(L, table_index, &ignored_length);
	if (shape == LuaTableShape::SparseArray) {
		error = kSparseArrayError;
		return false;
	}
	*root_as_array = shape == LuaTableShape::DenseArray;
	return true;
}

bool BuildRootBsonFromLuaTable(lua_State* L,
							   int table_index,
							   bool root_as_array,
							   int max_depth,
							   int lua_stack_reserve,
							   mongo::BsonDocument& doc,
							   std::string& error) {
	std::unordered_set<const void*> active_tables;
	if (!ValidateLuaTableDepthForBson(
			L, table_index, 0, max_depth, lua_stack_reserve, active_tables, error)) {
		return false;
	}
	active_tables.clear();
	return BuildBsonFromLuaTable(
		L, table_index, doc, root_as_array, 0, max_depth, lua_stack_reserve, active_tables, error);
}

char* SerializeBsonJson(const mongo::BsonDocument& doc,
						JsonMode mode,
						bool as_array,
						size_t* length) {
	if (as_array) {
		switch (mode) {
		case JsonMode::Canonical:
			return mongo::BsonDocument::ArrayAsCanonicalExtendedJson(doc, length);
		case JsonMode::Legacy: return mongo::BsonDocument::ArrayAsLegacyExtendedJson(doc, length);
		case JsonMode::Relaxed:
			return mongo::BsonDocument::ArrayAsRelaxedExtendedJson(doc, length);
		}
	}

	switch (mode) {
	case JsonMode::Canonical: return doc.AsCanonicalExtendedJson(length);
	case JsonMode::Legacy: return doc.AsLegacyExtendedJson(length);
	case JsonMode::Relaxed: return doc.AsJson(length);
	}

	return nullptr;
}

bool PushBsonJson(lua_State* L,
				  const mongo::BsonDocument& doc,
				  JsonMode mode,
				  bool as_array,
				  int max_depth,
				  int lua_stack_reserve,
				  std::string& error) {
	(void)lua_stack_reserve;
	if (!ValidateBsonDocumentForCodec(
			static_cast<const bson_t*>(doc.RawBson()), as_array, 0, max_depth, error)) {
		return false;
	}

	size_t len = 0;
	char* json = SerializeBsonJson(doc, mode, as_array, &len);
	if (!json) {
		error = "failed to serialize BSON document to JSON";
		return false;
	}
	lua_pushlstring(L, json, len);
	bson_free(json);
	return true;
}

int PushNilError(lua_State* L, const std::string& error) {
	lua_pushnil(L);
	lua_pushlstring(L, error.data(), error.size());
	return 2;
}

int l_to_bson(lua_State* L) {
	const int base_top = lua_gettop(L);
	if (!lua_istable(L, 1)) {
		return PushNilError(L, "db_bson.to_bson expects a table");
	}

	std::string error;
	TableOptions options;
	bool root_as_array = false;
	if (!ResolveRootTableOptions(
			L, 1, 2, "db_bson.to_bson", &options, &root_as_array, error)) {
		return PushNilError(L, error);
	}

	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		return PushNilError(L, "allocation failure");
	}

	if (!BuildRootBsonFromLuaTable(
			L, 1, root_as_array, options.max_depth, options.lua_stack_reserve, *doc, error)) {
		CLOUDENGINE_MEM_DELETE(doc);
		lua_settop(L, base_top);
		return PushNilError(L, error);
	}

	auto** ud = script::NewUserdata<mongo::BsonDocument>(L, kBsonDocMetaName);
	const int userdata_index = lua_gettop(L);
	*ud = doc;
	SetBsonUserdataRootArrayHint(L, userdata_index, root_as_array);
	return 1;
}

int l_to_table(lua_State* L) {
	const int base_top = lua_gettop(L);
	auto** doc_ud = static_cast<mongo::BsonDocument**>(luaL_testudata(L, 1, kBsonDocMetaName));
	if (!doc_ud) {
		if (lua_istable(L, 1)) {
			const WrapperType wrapper_type = GetWrapperType(L, 1);
			if (wrapper_type == WrapperType::Array || wrapper_type == WrapperType::Document) {
				std::string error;
				mongo::BsonDocument* wrapper_doc = nullptr;
				bool found = false;
				if (!GetRawBsonDocumentValue(L, 1, &wrapper_doc, &found, error)) {
					return PushNilError(L, error);
				}
				if (found) {
					TableOptions options;
					if (!ParseOptions(L, 2, true, &options, error)) {
						return PushNilError(L, error);
					}
					if (HasPublicTableFields(L, 1)) {
						return PushNilError(L, "BSON document wrapper must not contain Lua fields");
					}

					const bool root_as_array = wrapper_type == WrapperType::Array;
					const auto* raw = static_cast<const bson_t*>(wrapper_doc->RawBson());
					if (!ValidateBsonForLuaConversion(raw, error) ||
						!ValidateBsonDocumentForCodec(
							raw, root_as_array, 0, options.max_depth, error)) {
						return PushNilError(L, error);
					}
					if (!PushBsonDocumentAsTable(
							L,
							raw,
							root_as_array,
							options.preserve_types,
							0,
							options.max_depth,
							options.lua_stack_reserve,
							error)) {
						lua_settop(L, base_top);
						return PushNilError(L, error);
					}
					return 1;
				}
			}
		}
		return PushNilError(L, "db_bson.to_table expects bson.doc");
	}

	auto* doc = *doc_ud;
	if (!doc) {
		return PushNilError(L, "BSON document is not available");
	}

	const auto* raw = static_cast<const bson_t*>(doc->RawBson());
	std::string error;
	TableOptions options;
	if (!ParseOptions(L, 2, true, &options, error)) {
		return PushNilError(L, error);
	}
	if (!ValidateBsonForLuaConversion(raw, error)) {
		return PushNilError(L, error);
	}
	const bool root_as_array =
		options.root_as_array_set ? options.root_as_array : ResolveBsonUserdataAsArray(L, 1, raw);
	if (!ValidateBsonDocumentForCodec(raw, root_as_array, 0, options.max_depth, error)) {
		return PushNilError(L, error);
	}

	if (!PushBsonDocumentAsTable(
			L,
			raw,
			root_as_array,
			options.preserve_types,
			0,
			options.max_depth,
			options.lua_stack_reserve,
			error)) {
		lua_settop(L, base_top);
		return PushNilError(L, error);
	}
	return 1;
}

int l_to_json_common(lua_State* L, JsonMode mode, const char* function_name) {
	const int base_top = lua_gettop(L);
	std::string error;

	if (lua_istable(L, 1)) {
		TableOptions options;
		bool root_as_array = false;
		if (!ResolveRootTableOptions(
				L, 1, 2, function_name, &options, &root_as_array, error)) {
			return PushNilError(L, error);
		}

		mongo::BsonDocument doc;
		if (!BuildRootBsonFromLuaTable(
				L, 1, root_as_array, options.max_depth, options.lua_stack_reserve, doc, error)) {
			lua_settop(L, base_top);
			return PushNilError(L, error);
		}
		if (!PushBsonJson(
				L, doc, mode, root_as_array, options.max_depth, options.lua_stack_reserve, error)) {
			lua_settop(L, base_top);
			return PushNilError(L, error);
		}
		return 1;
	}

	auto** doc_ud = static_cast<mongo::BsonDocument**>(luaL_testudata(L, 1, kBsonDocMetaName));
	if (doc_ud) {
		auto* doc = *doc_ud;
		if (!doc) return PushNilError(L, "BSON document is not available");

		TableOptions options;
		if (!ParseOptions(L, 2, false, &options, error)) {
			return PushNilError(L, error);
		}

		const auto* raw = static_cast<const bson_t*>(doc->RawBson());
		if (!ValidateBsonForLuaConversion(raw, error)) {
			return PushNilError(L, error);
		}
		const bool root_as_array =
			options.root_as_array_set ? options.root_as_array : ResolveBsonUserdataAsArray(L, 1, raw);
		if (!PushBsonJson(
				L, *doc, mode, root_as_array, options.max_depth, options.lua_stack_reserve, error)) {
			return PushNilError(L, error);
		}
		return 1;
	}

	error = std::string(function_name) + " expects a table or bson.doc";
	return PushNilError(L, error);
}

int l_to_json(lua_State* L) {
	return l_to_json_common(L, JsonMode::Relaxed, "db_bson.to_json");
}

int l_to_relaxed_json(lua_State* L) {
	return l_to_json_common(L, JsonMode::Relaxed, "db_bson.to_relaxed_json");
}

int l_to_canonical_json(lua_State* L) {
	return l_to_json_common(L, JsonMode::Canonical, "db_bson.to_canonical_json");
}

int l_to_legacy_json(lua_State* L) {
	return l_to_json_common(L, JsonMode::Legacy, "db_bson.to_legacy_json");
}

int l_is_null(lua_State* L) {
	lua_pushboolean(L, IsNullSentinel(L, 1) ? 1 : 0);
	return 1;
}

int l_is_undefined(lua_State* L) {
	lua_pushboolean(L, IsUndefinedSentinel(L, 1) ? 1 : 0);
	return 1;
}

int l_is_min_key(lua_State* L) {
	lua_pushboolean(L, IsMinKeySentinel(L, 1) ? 1 : 0);
	return 1;
}

int l_is_max_key(lua_State* L) {
	lua_pushboolean(L, IsMaxKeySentinel(L, 1) ? 1 : 0);
	return 1;
}

int l_type(lua_State* L) {
	if (IsNullSentinel(L, 1)) {
		lua_pushstring(L, "null");
		return 1;
	}
	if (IsUndefinedSentinel(L, 1)) {
		lua_pushstring(L, "undefined");
		return 1;
	}
	if (IsMinKeySentinel(L, 1)) {
		lua_pushstring(L, "min_key");
		return 1;
	}
	if (IsMaxKeySentinel(L, 1)) {
		lua_pushstring(L, "max_key");
		return 1;
	}

	const WrapperType wrapper_type = GetWrapperType(L, 1);
	if (wrapper_type != WrapperType::None) {
		lua_pushstring(L, WrapperTypeName(wrapper_type));
		return 1;
	}

	lua_pushstring(L, luaL_typename(L, 1));
	return 1;
}

int l_array(lua_State* L) {
	if (lua_isnoneornil(L, 1)) {
		lua_newtable(L);
	} else if (lua_istable(L, 1)) {
		lua_pushvalue(L, 1);
	} else if (luaL_testudata(L, 1, kBsonDocMetaName)) {
		lua_newtable(L);
		SetRawBsonDocumentValue(L, -1, 1);
	} else {
		return PushNilError(L, "db_bson.array expects table, bson.doc, or nil");
	}
	SetWrapperType(L, -1, WrapperType::Array);
	return 1;
}

int l_document(lua_State* L) {
	if (lua_isnoneornil(L, 1)) {
		lua_newtable(L);
	} else if (lua_istable(L, 1)) {
		lua_pushvalue(L, 1);
	} else if (luaL_testudata(L, 1, kBsonDocMetaName)) {
		lua_newtable(L);
		SetRawBsonDocumentValue(L, -1, 1);
	} else {
		return PushNilError(L, "db_bson.document expects table, bson.doc, or nil");
	}
	SetWrapperType(L, -1, WrapperType::Document);
	return 1;
}

int l_int32(lua_State* L) {
	std::string error;
	int64_t raw_value = 0;
	if (!ReadLuaIntegerArgument(L, 1, "int32 value", &raw_value, error)) {
		return PushNilError(L, error);
	}

	int32_t value = 0;
	if (!ValidateInt32(raw_value, "int32 value", &value, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Int32);
	SetWrapperIntegerField(L, -1, "value", &kValueKey, value);
	return 1;
}

int l_int64(lua_State* L) {
	std::string error;
	int64_t value = 0;
	if (!ReadLuaIntegerArgument(L, 1, "int64 value", &value, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Int64);
	SetWrapperIntegerField(L, -1, "value", &kValueKey, value);
	return 1;
}

int l_double(lua_State* L) {
	std::string error;
	double value = 0.0;
	if (!ReadLuaNumberArgument(L, 1, "double value", &value, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Double);
	SetWrapperNumberField(L, -1, "value", &kValueKey, value);
	return 1;
}

int l_oid(lua_State* L) {
	size_t len = 0;
	const char* value = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "ObjectId", &value, &len, error)) {
		return PushNilError(L, error);
	}
	if (!ParseOid(std::string_view(value, len), nullptr, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Oid);
	SetWrapperStringField(L, -1, "value", &kValueKey, std::string_view(value, len));
	return 1;
}

int l_datetime(lua_State* L) {
	std::string error;
	int64_t value = 0;
	if (!ReadLuaIntegerArgument(L, 1, "datetime value", &value, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::DateTime);
	SetWrapperIntegerField(L, -1, "value", &kValueKey, value);
	return 1;
}

int l_timestamp(lua_State* L) {
	int64_t timestamp_value = 0;
	int64_t increment_value = 0;
	uint32_t timestamp = 0;
	uint32_t increment = 0;
	std::string error;
	if (!ReadLuaIntegerArgument(L, 1, "timestamp", &timestamp_value, error) ||
		!ReadOptionalLuaIntegerArgument(
			L, 2, 0, "timestamp increment", &increment_value, error) ||
		!ValidateUInt32(timestamp_value, "timestamp", &timestamp, error) ||
		!ValidateUInt32(increment_value, "timestamp increment", &increment, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Timestamp);
	SetWrapperIntegerField(L, -1, "timestamp", &kValueKey, timestamp);
	SetWrapperIntegerField(L, -1, "increment", &kIncrementKey, increment);
	return 1;
}

int l_binary(lua_State* L) {
	size_t len = 0;
	const char* value = nullptr;
	int64_t subtype_value = 0;
	uint8_t subtype = 0;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "binary value", &value, &len, error) ||
		!ReadOptionalLuaIntegerArgument(L, 2, 0, "binary subtype", &subtype_value, error) ||
		!ValidateByte(subtype_value, "binary subtype", &subtype, error)) {
		return PushNilError(L, error);
	}
	if (len > std::numeric_limits<uint32_t>::max()) {
		return PushNilError(L, "binary value is too large for BSON");
	}

	PushTaggedTable(L, WrapperType::Binary);
	SetWrapperStringField(L, -1, "value", &kValueKey, std::string_view(value, len));
	SetWrapperIntegerField(L, -1, "subtype", &kSubtypeKey, subtype);
	return 1;
}

int l_regex(lua_State* L) {
	size_t pattern_len = 0;
	size_t options_len = 0;
	const char* pattern = nullptr;
	const char* options = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "regex pattern", &pattern, &pattern_len, error) ||
		!ReadOptionalLuaStringArgument(
			L, 2, "", "regex options", &options, &options_len, error)) {
		return PushNilError(L, error);
	}
	const std::string_view pattern_view(pattern, pattern_len);
	const std::string_view options_view(options, options_len);
	if (!EnsureValidBsonCString(pattern_view, "regex pattern", error) ||
		!EnsureValidBsonCString(options_view, "regex options", error) ||
		!EnsureValidRegexOptions(options_view, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Regex);
	SetWrapperStringField(L, -1, "pattern", &kValueKey, pattern_view);
	SetWrapperStringField(L, -1, "options", &kOptionsKey, options_view);
	return 1;
}

int l_code(lua_State* L) {
	size_t len = 0;
	const char* value = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "code value", &value, &len, error)) {
		return PushNilError(L, error);
	}
	if (!EnsureValidBsonCString(std::string_view(value, len), "code value", error)) {
		return PushNilError(L, error);
	}
	if (!lua_isnoneornil(L, 2) && !lua_istable(L, 2) &&
		!luaL_testudata(L, 2, kBsonDocMetaName)) {
		return PushNilError(L, "code scope must be a document table or bson.doc");
	}

	PushTaggedTable(L, WrapperType::Code);
	const int wrapper_index = lua_gettop(L);
	SetWrapperStringField(L, wrapper_index, "value", &kValueKey, std::string_view(value, len));
	if (!lua_isnoneornil(L, 2)) {
		lua_pushvalue(L, 2);
		lua_rawsetp(L, wrapper_index, &kScopeKey);
		lua_pushvalue(L, 2);
		lua_setfield(L, wrapper_index, "scope");
	}
	return 1;
}

int l_symbol(lua_State* L) {
	size_t len = 0;
	const char* value = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "symbol value", &value, &len, error)) {
		return PushNilError(L, error);
	}
	if (!ValidateBsonIntLength(len, "symbol value", error) ||
		!EnsureValidBsonText(std::string_view(value, len), "symbol value", error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::Symbol);
	SetWrapperStringField(L, -1, "value", &kValueKey, std::string_view(value, len));
	return 1;
}

int l_decimal128(lua_State* L) {
	size_t len = 0;
	const char* value = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(L, 1, "decimal128 value", &value, &len, error)) {
		return PushNilError(L, error);
	}
	if (!EnsureNoEmbeddedNull(std::string_view(value, len), "decimal128 value", error)) {
		return PushNilError(L, error);
	}

	mongo::MongoDecimal128 decimal;
	if (!decimal.FromString(std::string(value, len).c_str())) {
		return PushNilError(L, "decimal128 value is invalid");
	}

	PushTaggedTable(L, WrapperType::Decimal128);
	SetWrapperStringField(L, -1, "value", &kValueKey, std::string_view(value, len));
	return 1;
}

int l_dbpointer(lua_State* L) {
	size_t collection_len = 0;
	size_t oid_len = 0;
	const char* collection = nullptr;
	const char* oid = nullptr;
	std::string error;
	if (!ReadLuaStringArgument(
			L, 1, "dbpointer collection", &collection, &collection_len, error) ||
		!ReadLuaStringArgument(L, 2, "dbpointer oid", &oid, &oid_len, error) ||
		!EnsureValidBsonCString(
			std::string_view(collection, collection_len), "dbpointer collection", error) ||
		!ParseOid(std::string_view(oid, oid_len), nullptr, error)) {
		return PushNilError(L, error);
	}

	PushTaggedTable(L, WrapperType::DBPointer);
	SetWrapperStringField(
		L, -1, "collection", &kValueKey, std::string_view(collection, collection_len));
	SetWrapperStringField(L, -1, "oid", &kOidKey, std::string_view(oid, oid_len));
	return 1;
}

const luaL_Reg kDbBsonFunctions[] = {
	{"to_bson", l_to_bson},
	{"from_table", l_to_bson},
	{"to_table", l_to_table},
	{"to_json", l_to_json},
	{"to_relaxed_json", l_to_relaxed_json},
	{"to_canonical_json", l_to_canonical_json},
	{"to_legacy_json", l_to_legacy_json},
	{"is_null", l_is_null},
	{"is_undefined", l_is_undefined},
	{"is_min_key", l_is_min_key},
	{"is_max_key", l_is_max_key},
	{"type", l_type},
	{"array", l_array},
	{"document", l_document},
	{"int32", l_int32},
	{"int64", l_int64},
	{"double", l_double},
	{"oid", l_oid},
	{"datetime", l_datetime},
	{"timestamp", l_timestamp},
	{"binary", l_binary},
	{"regex", l_regex},
	{"code", l_code},
	{"symbol", l_symbol},
	{"decimal128", l_decimal128},
	{"dbpointer", l_dbpointer},
	{nullptr, nullptr},
};

}  // namespace

void ExportDbBsonCodec(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return;

	script::RegisterBsonDocumentMeta(L);

	lua_newtable(L);
	luaL_setfuncs(L, kDbBsonFunctions, 0);
	PushNullSentinel(L);
	lua_setfield(L, -2, "null");
	PushUndefinedSentinel(L);
	lua_setfield(L, -2, "undefined");
	PushMinKeySentinel(L);
	lua_setfield(L, -2, "min_key");
	PushMaxKeySentinel(L);
	lua_setfield(L, -2, "max_key");

	lua_pushvalue(L, -1);
	lua_setglobal(L, kModuleName);

	lua_getglobal(L, "package");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "loaded");
		if (lua_istable(L, -1)) {
			lua_pushvalue(L, -3);
			lua_setfield(L, -2, kModuleName);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_pop(L, 1);
}

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
