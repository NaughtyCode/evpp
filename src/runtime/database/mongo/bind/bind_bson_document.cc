#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_bson_document.h"

#include <cstdint>
#include <cstring>
#include <string>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_oid.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.doc";

int l_bson_doc_gc(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(doc);
	*CheckUserdata<mongo::BsonDocument>(L, 1, kMetaName) = nullptr;
	return 0;
}

// ── Constructors ──────────────────────────────────────────────────────

int l_bson_doc_new(lua_State* L) {
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = doc;
	return 1;
}

int l_bson_doc_destroy(lua_State* L) {
	l_bson_doc_gc(L);
	return 0;
}

int l_bson_doc_from_json(lua_State* L) {
	size_t len;
	const char* json = luaL_checklstring(L, 1, &len);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, 
		mongo::BsonDocument::NewFromJson(reinterpret_cast<const uint8_t*>(json), len));
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = doc;
	return 1;
}

int l_bson_doc_from_data(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, 
		mongo::BsonDocument::NewFromData(reinterpret_cast<const uint8_t*>(data), len));
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = doc;
	return 1;
}

// ── Serialization ─────────────────────────────────────────────────────

int l_bson_doc_as_json(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	std::string json = doc->ToJson();
	lua_pushlstring(L, json.data(), json.size());
	return 1;
}

int l_bson_doc_get_data(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	const uint8_t* data = doc->GetData();
	uint32_t len = doc->GetLength();
	lua_pushlstring(L, reinterpret_cast<const char*>(data), len);
	return 1;
}

// ── Append fields ─────────────────────────────────────────────────────

int l_bson_doc_append_int32(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto val = luaL_checkinteger(L, 3);
	lua_pushboolean(L,
					doc && val <= INT32_MAX && val >= INT32_MIN &&
						doc->AppendInt32(key, static_cast<int32_t>(val)));
	return 1;
}

int l_bson_doc_append_int64(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto value = static_cast<int64_t>(luaL_checkinteger(L, 3));
	lua_pushboolean(L, doc && doc->AppendInt64(key, value));
	return 1;
}

int l_bson_doc_append_double(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	double value = static_cast<double>(luaL_checknumber(L, 3));
	lua_pushboolean(L, doc && doc->AppendDouble(key, value));
	return 1;
}

int l_bson_doc_append_utf8(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	size_t len;
	const char* value = luaL_checklstring(L, 3, &len);
	lua_pushboolean(L, doc && doc->AppendUtf8(key, std::string_view(value, len)));
	return 1;
}

int l_bson_doc_append_bool(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	bool value = lua_toboolean(L, 3) != 0;
	lua_pushboolean(L, doc && doc->AppendBool(key, value));
	return 1;
}

int l_bson_doc_append_oid(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* oid_str = luaL_checkstring(L, 3);
	mongo::MongoOid oid;
	if (!oid.IsValid(oid_str, strlen(oid_str))) {
		lua_pushboolean(L, false);
		return 1;
	}
	oid.InitFromString(oid_str);
	lua_pushboolean(L, doc && doc->AppendOid(key, oid));
	return 1;
}

int l_bson_doc_append_null(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->AppendNull(key));
	return 1;
}

int l_bson_doc_append_document(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 3, kMetaName);
	lua_pushboolean(L, doc && subdoc && doc->AppendDocument(key, *subdoc));
	return 1;
}

int l_bson_doc_append_timestamp(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto ts = luaL_checkinteger(L, 3);
	auto inc = luaL_checkinteger(L, 4);
	lua_pushboolean(
		L,
		doc && ts >= 0 && ts <= UINT32_MAX && inc >= 0 && inc <= UINT32_MAX &&
			doc->AppendTimestamp(key, static_cast<uint32_t>(ts), static_cast<uint32_t>(inc)));
	return 1;
}

int l_bson_doc_append_datetime(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto value = static_cast<int64_t>(luaL_checkinteger(L, 3));
	lua_pushboolean(L, doc && doc->AppendDateTime(key, value));
	return 1;
}

int l_bson_doc_append_binary(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto subtype = CheckIntegerArg<int>(L, 3);
	size_t len;
	const char* data = luaL_checklstring(L, 4, &len);
	lua_pushboolean(
		L,
		doc && len <= UINT32_MAX &&
			doc->AppendBinary(
				key, subtype, reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len)));
	return 1;
}

int l_bson_doc_append_array(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* array_doc = GetUserdata<mongo::BsonDocument>(L, 3, kMetaName);
	lua_pushboolean(L, doc && array_doc && doc->AppendArray(key, *array_doc));
	return 1;
}

int l_bson_doc_append_regex(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* regex = luaL_checkstring(L, 3);
	const char* options = luaL_checkstring(L, 4);
	lua_pushboolean(L, doc && doc->AppendRegex(key, regex, options));
	return 1;
}

int l_bson_doc_append_code(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* code = luaL_checkstring(L, 3);
	lua_pushboolean(L, doc && doc->AppendCode(key, code));
	return 1;
}

int l_bson_doc_append_symbol(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* symbol = luaL_checkstring(L, 3);
	lua_pushboolean(L, doc && doc->AppendSymbol(key, symbol));
	return 1;
}

int l_bson_doc_append_minkey(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->AppendMinkey(key));
	return 1;
}

int l_bson_doc_append_maxkey(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->AppendMaxkey(key));
	return 1;
}

int l_bson_doc_append_undefined(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->AppendUndefined(key));
	return 1;
}

int l_bson_doc_append_now_utc(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->AppendNowUtc(key));
	return 1;
}

int l_bson_doc_append_decimal128(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* dec_str = luaL_checkstring(L, 3);
	mongo::MongoDecimal128 dec;
	if (!dec.FromString(dec_str)) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, doc && doc->AppendDecimal128(key, dec));
	return 1;
}

int l_bson_doc_append_timeval(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto tv_sec = CheckIntegerArg<long>(L, 3);
	auto tv_usec = CheckIntegerArg<long>(L, 4);
	struct timeval tv;
	tv.tv_sec = tv_sec;
	tv.tv_usec = tv_usec;
	lua_pushboolean(L, doc && doc->AppendTimeval(key, &tv));
	return 1;
}

// ── Copy / utility ─────────────────────────────────────────────────────

int l_bson_doc_copy(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, doc->Copy());
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = copy;
	return 1;
}

int l_bson_doc_clear(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (doc) doc->Clear();
	return 0;
}

int l_bson_doc_equal(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* other = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	lua_pushboolean(L, doc && other && doc->Equal(*other));
	return 1;
}

int l_bson_doc_concat(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* src = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	lua_pushboolean(L, doc && src && doc->Concat(*src));
	return 1;
}

int l_bson_doc_compare(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* other = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	if (!doc || !other) {
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, doc->Compare(*other));
	return 1;
}

int l_bson_doc_init_from_json(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushboolean(L, false);
		return 1;
	}
	size_t len;
	const char* json = luaL_checklstring(L, 2, &len);
	mongo::MongoError error;
	lua_pushboolean(L, doc->InitFromJson(json, static_cast<int64_t>(len), &error));
	return 1;
}

int l_bson_doc_validate(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushboolean(L, false);
		lua_pushnil(L);
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	bool ok = doc->Validate(&error);
	lua_pushboolean(L, ok);
	if (ok)
		lua_pushnil(L);
	else {
		const char* msg = error.Message();
		lua_pushstring(L, msg ? msg : "unknown error");
	}
	lua_pushnil(L);
	return 3;
}

// ── Sub-document building ──────────────────────────────────────────────

int l_bson_doc_append_document_begin(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* subdoc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!subdoc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	if (!doc || !doc->AppendDocumentBegin(key, subdoc)) {
		CLOUDENGINE_MEM_DELETE(subdoc);
		lua_pushboolean(L, false);
		return 1;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = subdoc;
	return 1;
}

int l_bson_doc_append_document_end(lua_State* L) {
	auto* parent = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	lua_pushboolean(L, parent && subdoc && mongo::BsonDocument::AppendDocumentEnd(parent, subdoc));
	return 1;
}

int l_bson_doc_append_array_begin(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* array = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!array) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	if (!doc || !doc->AppendArrayBegin(key, array)) {
		CLOUDENGINE_MEM_DELETE(array);
		lua_pushboolean(L, false);
		return 1;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = array;
	return 1;
}

int l_bson_doc_append_array_end(lua_State* L) {
	auto* parent = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* array = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	lua_pushboolean(L, parent && array && mongo::BsonDocument::AppendArrayEnd(parent, array));
	return 1;
}

int l_bson_doc_append_array_unsafe_begin(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* child = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!child) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	if (!doc || !doc->AppendArrayUnsafeBegin(key, child)) {
		CLOUDENGINE_MEM_DELETE(child);
		lua_pushboolean(L, false);
		return 1;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = child;
	return 1;
}

int l_bson_doc_append_array_builder_begin(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid document");
		lua_pushnil(L);
		return 3;
	}
	void* builder = nullptr;
	if (!doc->AppendArrayBuilderBegin(key, &builder)) {
		lua_pushnil(L);
		lua_pushstring(L, "append array builder failed");
		lua_pushnil(L);
		return 3;
	}
	lua_pushlightuserdata(L, builder);
	return 1;
}

int l_bson_doc_append_array_builder_end(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	void* builder = lua_touserdata(L, 2);
	lua_pushboolean(L, doc && builder && mongo::BsonDocument::AppendArrayBuilderEnd(doc, builder));
	return 1;
}

// ── Additional append methods ──────────────────────────────────────────

int l_bson_doc_append_code_with_scope(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* code = luaL_checkstring(L, 3);
	auto* scope = GetUserdata<mongo::BsonDocument>(L, 4, kMetaName);
	lua_pushboolean(L, doc && scope && doc->AppendCodeWithScope(key, code, *scope));
	return 1;
}

int l_bson_doc_append_dbref(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	const char* collection = luaL_checkstring(L, 3);
	const char* oid_str = luaL_checkstring(L, 4);
	mongo::MongoOid oid;
	if (!oid.IsValid(oid_str, strlen(oid_str))) {
		lua_pushboolean(L, false);
		return 1;
	}
	oid.InitFromString(oid_str);
	lua_pushboolean(L, doc && doc->AppendDBPointer(key, collection, oid));
	return 1;
}

int l_bson_doc_append_timet(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto val = CheckIntegerArg<time_t>(L, 3);
	lua_pushboolean(L, doc && doc->AppendTimeT(key, val));
	return 1;
}

int l_bson_doc_append_regex_wlen(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto keylen = CheckIntegerArg<int>(L, 3);
	const char* regex = luaL_checkstring(L, 4);
	const char* options = luaL_checkstring(L, 5);
	lua_pushboolean(L, doc && doc->AppendRegexWLen(key, keylen, regex, options));
	return 1;
}

int l_bson_doc_append_value(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	// raw bson_value_t passed as opaque userdata pointer
	const void* value = lua_touserdata(L, 3);
	lua_pushboolean(L, doc && value && doc->AppendValue(key, value));
	return 1;
}

int l_bson_doc_append_iter(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 3, "bson.iter");
	lua_pushboolean(L, doc && iter && doc->AppendIter(key, *iter));
	return 1;
}

int l_bson_doc_append_binary_uninit(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto subtype = CheckIntegerArg<int>(L, 3);
	auto len = CheckIntegerArg<uint32_t>(L, 4);
	uint8_t* data_out = nullptr;
	bool ok = doc && doc->AppendBinaryUninit(key, subtype, &data_out, len);
	lua_pushboolean(L, ok);
	if (ok && data_out)
		lua_pushlightuserdata(L, data_out);
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bson_doc_append_array_from_vector(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 3, "bson.iter");
	lua_pushboolean(L, doc && iter && doc->AppendArrayFromVector(key, *iter));
	return 1;
}

// ── Copy / utility additions ────────────────────────────────────────────

int l_bson_doc_copy_to(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* dst = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	lua_pushboolean(L, doc && dst && doc->CopyTo(*dst));
	return 1;
}

int l_bson_doc_reserve_buffer(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto size = CheckIntegerArg<uint32_t>(L, 2);
	lua_pushboolean(L, doc && doc->ReserveBuffer(size));
	return 1;
}

// ── Additional JSON output methods ─────────────────────────────────────

int l_bson_doc_as_legacy_extended_json(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = doc->AsLegacyExtendedJson(&len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_as_json_with_opts(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	// opts passed as opaque pointer (bson_json_opts_t*) via lightuserdata
	const void* opts = lua_touserdata(L, 2);
	size_t len = 0;
	char* str = doc->AsJsonWithOpts(&len, opts);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_array_as_canonical_extended_json(lua_State* L) {
	auto* array = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!array) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = mongo::BsonDocument::ArrayAsCanonicalExtendedJson(*array, &len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_array_as_relaxed_extended_json(lua_State* L) {
	auto* array = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!array) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = mongo::BsonDocument::ArrayAsRelaxedExtendedJson(*array, &len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_array_as_legacy_extended_json(lua_State* L) {
	auto* array = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!array) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = mongo::BsonDocument::ArrayAsLegacyExtendedJson(*array, &len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// ── Additional static initializers ─────────────────────────────────────

int l_bson_doc_new_from_buffer(lua_State* L) {
	// Takes existing data + realloc func as lightuserdata; advanced use.
	lua_pushnil(L);
	lua_pushstring(L, "new_from_buffer requires buffer and realloc  - use from_data");
	lua_pushnil(L);
	return 3;
}

int l_bson_doc_sized_new(lua_State* L) {
	auto size = CheckIntegerArg<size_t>(L, 1);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, mongo::BsonDocument::SizedNew(size));
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
	*ud = doc;
	return 1;
}

int l_bson_doc_validate_with_error_and_offset(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushboolean(L, false);
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 3;
	}
	mongo::MongoError error;
	size_t offset = 0;
	bool ok = doc->ValidateWithErrorAndOffset(&error, &offset);
	lua_pushboolean(L, ok);
	if (ok) {
		lua_pushnil(L);
		lua_pushinteger(L, 0);
	} else {
		lua_pushstring(L, error.Message() ? error.Message() : "unknown error");
		lua_pushinteger(L, static_cast<lua_Integer>(offset));
	}
	return 3;
}

// ── Query / access ────────────────────────────────────────────────────

int l_bson_doc_count_keys(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	lua_pushinteger(L, doc ? doc->CountKeys() : 0);
	return 1;
}

int l_bson_doc_has_field(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, doc && doc->HasField(key));
	return 1;
}

int l_bson_doc_empty(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	lua_pushboolean(L, doc && doc->Empty());
	return 1;
}

int l_bson_doc_get_length(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	lua_pushinteger(L, doc ? doc->GetLength() : 0);
	return 1;
}

int l_bson_doc_reinit(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (doc) doc->Reinit();
	return 0;
}

int l_bson_doc_as_canonical_extended_json(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = doc->AsCanonicalExtendedJson(&len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_as_relaxed_extended_json(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = 0;
	char* str = doc->AsRelaxedExtendedJson(&len);
	if (str) {
		lua_pushlstring(L, str, len);
		bson_free(str);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_bson_doc_steal(lua_State* L) {
	auto* dst = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
	auto* src = GetUserdata<mongo::BsonDocument>(L, 2, kMetaName);
	if (dst && src) mongo::BsonDocument::Steal(*dst, *src);
	return 0;
}

const luaL_Reg kLib[] = {
	{"new", l_bson_doc_new},
	{"destroy", l_bson_doc_destroy},
	{"from_json", l_bson_doc_from_json},
	{"from_data", l_bson_doc_from_data},
	{"sized_new", l_bson_doc_sized_new},
	{"as_json", l_bson_doc_as_json},
	{"get_data", l_bson_doc_get_data},
	{"append_int32", l_bson_doc_append_int32},
	{"append_int64", l_bson_doc_append_int64},
	{"append_double", l_bson_doc_append_double},
	{"append_utf8", l_bson_doc_append_utf8},
	{"append_bool", l_bson_doc_append_bool},
	{"append_oid", l_bson_doc_append_oid},
	{"append_null", l_bson_doc_append_null},
	{"append_document", l_bson_doc_append_document},
	{"append_timestamp", l_bson_doc_append_timestamp},
	{"append_datetime", l_bson_doc_append_datetime},
	{"append_binary", l_bson_doc_append_binary},
	{"append_array", l_bson_doc_append_array},
	{"append_regex", l_bson_doc_append_regex},
	{"append_code", l_bson_doc_append_code},
	{"append_code_with_scope", l_bson_doc_append_code_with_scope},
	{"append_symbol", l_bson_doc_append_symbol},
	{"append_minkey", l_bson_doc_append_minkey},
	{"append_maxkey", l_bson_doc_append_maxkey},
	{"append_undefined", l_bson_doc_append_undefined},
	{"append_now_utc", l_bson_doc_append_now_utc},
	{"append_decimal128", l_bson_doc_append_decimal128},
	{"append_dbref", l_bson_doc_append_dbref},
	{"append_timet", l_bson_doc_append_timet},
	{"append_timeval", l_bson_doc_append_timeval},
	{"append_regex_wlen", l_bson_doc_append_regex_wlen},
	{"append_value", l_bson_doc_append_value},
	{"append_iter", l_bson_doc_append_iter},
	{"append_binary_uninit", l_bson_doc_append_binary_uninit},
	{"append_array_from_vector", l_bson_doc_append_array_from_vector},
	{"append_document_begin", l_bson_doc_append_document_begin},
	{"append_document_end", l_bson_doc_append_document_end},
	{"append_array_begin", l_bson_doc_append_array_begin},
	{"append_array_end", l_bson_doc_append_array_end},
	{"append_array_unsafe_begin", l_bson_doc_append_array_unsafe_begin},
	{"append_array_builder_begin", l_bson_doc_append_array_builder_begin},
	{"append_array_builder_end", l_bson_doc_append_array_builder_end},
	{"count_keys", l_bson_doc_count_keys},
	{"has_field", l_bson_doc_has_field},
	{"empty", l_bson_doc_empty},
	{"get_length", l_bson_doc_get_length},
	{"reinit", l_bson_doc_reinit},
	{"as_canonical_extended_json", l_bson_doc_as_canonical_extended_json},
	{"as_relaxed_extended_json", l_bson_doc_as_relaxed_extended_json},
	{"as_legacy_extended_json", l_bson_doc_as_legacy_extended_json},
	{"as_json_with_opts", l_bson_doc_as_json_with_opts},
	{"array_as_canonical_extended_json", l_bson_doc_array_as_canonical_extended_json},
	{"array_as_relaxed_extended_json", l_bson_doc_array_as_relaxed_extended_json},
	{"array_as_legacy_extended_json", l_bson_doc_array_as_legacy_extended_json},
	{"copy", l_bson_doc_copy},
	{"clear", l_bson_doc_clear},
	{"copy_to", l_bson_doc_copy_to},
	{"reserve_buffer", l_bson_doc_reserve_buffer},
	{"equal", l_bson_doc_equal},
	{"concat", l_bson_doc_concat},
	{"compare", l_bson_doc_compare},
	{"init_from_json", l_bson_doc_init_from_json},
	{"validate", l_bson_doc_validate},
	{"validate_with_error_and_offset", l_bson_doc_validate_with_error_and_offset},
	{"steal", l_bson_doc_steal},
	{nullptr, nullptr},
};

}  // namespace

void RegisterBsonDocumentMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bson_doc_gc);
}

const luaL_Reg* GetBsonDocumentLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
