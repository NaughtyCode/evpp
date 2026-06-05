#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_bson_iter.h"

#include <cstdint>
#include <limits>
#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_oid.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.iter";

int l_bson_iter_gc(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(iter);
	*CheckUserdata<mongo::BsonIter>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bson_iter_new(lua_State* L) {
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, "bson.doc");
	if (!doc) {
		lua_pushnil(L);
		return 1;
	}
	auto* iter = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonIter, *doc);
	if (!iter) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonIter>(L, kMetaName);
	*ud = iter;
	return 1;
}

int l_bson_iter_next(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushboolean(L, iter && iter->Next());
	return 1;
}

int l_bson_iter_find(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, iter && iter->Find(key));
	return 1;
}

int l_bson_iter_key(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* key = iter ? iter->Key() : nullptr;
	if (key)
		lua_pushstring(L, key);
	else
		lua_pushnil(L);
	return 1;
}

int l_bson_iter_type(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->Type() : 0);
	return 1;
}

int l_bson_iter_as_int32(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->AsInt32() : 0);
	return 1;
}

int l_bson_iter_as_int64(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->AsInt64() : 0);
	return 1;
}

int l_bson_iter_as_double(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushnumber(L, iter ? iter->AsDouble() : 0.0);
	return 1;
}

int l_bson_iter_as_utf8(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t len;
	const char* s = iter->AsUtf8(&len);
	if (s)
		lua_pushlstring(L, s, len);
	else
		lua_pushnil(L);
	return 1;
}

int l_bson_iter_as_bool(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushboolean(L, iter && iter->AsBool());
	return 1;
}

int l_bson_iter_as_datetime(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->AsDateTime() : 0);
	return 1;
}

int l_bson_iter_as_oid(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoOid oid = iter->AsOid();
	std::string s = oid.ToString();
	lua_pushlstring(L, s.data(), s.size());
	return 1;
}

int l_bson_iter_recurse(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	auto* sub = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonIter, iter->Recurse());
	if (!sub) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonIter>(L, kMetaName);
	*ud = sub;
	return 1;
}

int l_bson_iter_as_binary(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	int subtype;
	uint32_t len;
	const uint8_t* data;
	iter->AsBinary(&subtype, &len, &data);
	lua_pushlstring(L, reinterpret_cast<const char*>(data), len);
	lua_pushinteger(L, subtype);
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_offset(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->Offset() : 0);
	return 1;
}

// ── Document / array accessors ──────────────────────────────────────────

int l_bson_iter_as_document(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t len;
	const uint8_t* data;
	iter->AsDocument(&len, &data);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, data, len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_bson_iter_as_array(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t len;
	const uint8_t* data;
	iter->AsArray(&len, &data);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, data, len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_bson_iter_as_timestamp(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t timestamp, increment;
	iter->AsTimestamp(&timestamp, &increment);
	lua_pushinteger(L, timestamp);
	lua_pushinteger(L, increment);
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_as_int64_coerce(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->AsInt64Coerce() : 0);
	return 1;
}

int l_bson_iter_as_double_coerce(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushnumber(L, iter ? iter->AsDoubleCoerce() : 0.0);
	return 1;
}

int l_bson_iter_as_code(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t len;
	const char* s = iter->AsCode(&len);
	if (s)
		lua_pushlstring(L, s, len);
	else
		lua_pushnil(L);
	return 1;
}

int l_bson_iter_as_regex(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	const char* regex;
	const char* options;
	iter->AsRegex(&regex, &options);
	lua_pushstring(L, regex ? regex : "");
	lua_pushstring(L, options ? options : "");
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_as_symbol(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	const char* sym = iter->AsSymbol();
	if (sym)
		lua_pushstring(L, sym);
	else
		lua_pushnil(L);
	return 1;
}

// ── Find variants ──────────────────────────────────────────────────────

int l_bson_iter_find_case(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, iter && iter->FindCase(key));
	return 1;
}

int l_bson_iter_find_descendant(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* dotkey = luaL_checkstring(L, 2);
	if (!iter) {
		lua_pushboolean(L, false);
		return 1;
	}
	auto* desc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonIter);
	if (!desc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	bool ok = iter->FindDescendant(dotkey, desc);
	if (!ok) {
		CLOUDENGINE_MEM_DELETE(desc);
		lua_pushboolean(L, false);
		return 1;
	}
	auto** ud = NewUserdata<mongo::BsonIter>(L, kMetaName);
	*ud = desc;
	lua_pushboolean(L, true);
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_find_wlen(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* key = luaL_checkstring(L, 2);
	auto keylen = CheckIntegerArg<int>(L, 3);
	lua_pushboolean(L, iter && iter->FindWLen(key, keylen));
	return 1;
}

// ── Init-and-find ──────────────────────────────────────────────────────

int l_bson_iter_init_find(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	const char* key = luaL_checkstring(L, 3);
	lua_pushboolean(L, iter && doc && iter->InitFind(*doc, key));
	return 1;
}

int l_bson_iter_init_find_case(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	const char* key = luaL_checkstring(L, 3);
	lua_pushboolean(L, iter && doc && iter->InitFindCase(*doc, key));
	return 1;
}

int l_bson_iter_init_find_wlen(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	const char* key = luaL_checkstring(L, 3);
	auto keylen = CheckIntegerArg<int>(L, 4);
	lua_pushboolean(L, iter && doc && iter->InitFindWLen(*doc, key, keylen));
	return 1;
}

int l_bson_iter_init_from_data(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	lua_pushboolean(L, iter && iter->InitFromData(reinterpret_cast<const uint8_t*>(data), len));
	return 1;
}

// ── Key metadata ───────────────────────────────────────────────────────

int l_bson_iter_key_len(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->KeyLen() : 0);
	return 1;
}

int l_bson_iter_dup_utf8(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t len;
	char* s = iter->DupUtf8(&len);
	if (s) {
		lua_pushlstring(L, s, len);
		bson_free(s);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// ── Binary subtype ─────────────────────────────────────────────────────

int l_bson_iter_binary_subtype(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? iter->BinarySubtype() : 0);
	return 1;
}

// ── Bool coerce ────────────────────────────────────────────────────────

int l_bson_iter_as_bool_coerce(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushboolean(L, iter && iter->AsBoolCoerce());
	return 1;
}

// ── DBPointer access ───────────────────────────────────────────────────

int l_bson_iter_as_dbref(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t coll_len;
	const char* collection;
	const void* oid;
	iter->AsDBPointer(&coll_len, &collection, &oid);
	if (collection)
		lua_pushlstring(L, collection, coll_len);
	else
		lua_pushnil(L);
	const uint8_t* oid_bytes = static_cast<const uint8_t*>(oid);
	if (oid_bytes) {
		mongo::MongoOid m_oid;
		m_oid.InitFromData(oid_bytes);
		std::string s = m_oid.ToString();
		lua_pushlstring(L, s.data(), s.size());
	} else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

// ── Raw value access ───────────────────────────────────────────────────

int l_bson_iter_value(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	const void* val = iter->Value();
	if (val)
		lua_pushlightuserdata(L, const_cast<void*>(val));
	else
		lua_pushnil(L);
	return 1;
}

// ── Overwrite methods ──────────────────────────────────────────────────

int l_bson_iter_overwrite_int32(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (iter) iter->OverwriteInt32(val);
	return 0;
}

int l_bson_iter_overwrite_int64(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (iter) iter->OverwriteInt64(val);
	return 0;
}

int l_bson_iter_overwrite_double(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	double val = static_cast<double>(luaL_checknumber(L, 2));
	if (iter) iter->OverwriteDouble(val);
	return 0;
}

int l_bson_iter_overwrite_bool(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (iter) iter->OverwriteBool(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_bson_iter_overwrite_datetime(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (iter) iter->OverwriteDateTime(val);
	return 0;
}

int l_bson_iter_overwrite_timestamp(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto ts = CheckIntegerArg<uint32_t>(L, 2);
	auto inc = CheckIntegerArg<uint32_t>(L, 3);
	if (iter) iter->OverwriteTimestamp(ts, inc);
	return 0;
}

int l_bson_iter_overwrite_oid(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* oid_str = luaL_checkstring(L, 2);
	mongo::MongoOid oid;
	if (!oid.IsValid(oid_str, strlen(oid_str))) return 0;
	oid.InitFromString(oid_str);
	if (iter) iter->OverwriteOid(oid);
	return 0;
}

int l_bson_iter_overwrite_decimal128(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	const char* dec_str = luaL_checkstring(L, 2);
	mongo::MongoDecimal128 dec;
	if (!dec.FromString(dec_str)) return 0;
	if (iter) iter->OverwriteDecimal128(dec);
	return 0;
}

int l_bson_iter_overwrite_binary(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto subtype = CheckIntegerArg<int>(L, 2);
	size_t bin_len;
	const char* data = luaL_checklstring(L, 3, &bin_len);
	if (bin_len > (std::numeric_limits<uint32_t>::max)()) {
		return luaL_argerror(L, 3, "binary data too large");
	}
	if (!iter) return 0;
	// Note: OverwriteBinary takes uint32_t* for binary_len (in-out param)
	uint32_t len32 = static_cast<uint32_t>(bin_len);
	uint8_t* bin_out = nullptr;
	iter->OverwriteBinary(subtype, &len32, &bin_out);
	if (bin_out && len32 >= bin_len) {
		memcpy(bin_out, data, bin_len);
	}
	return 0;
}

int l_bson_iter_as_decimal128(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoDecimal128 dec;
	if (!iter->AsDecimal128(&dec)) {
		lua_pushnil(L);
		return 1;
	}
	std::string s = dec.ToString();
	lua_pushlstring(L, s.data(), s.size());
	return 1;
}

int l_bson_iter_as_code_with_scope(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	uint32_t code_len = 0;
	const char* code_cstr = nullptr;
	auto* scope = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!scope) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	iter->AsCodeWithScope(&code_len, &code_cstr, scope);
	if (!code_cstr) {
		CLOUDENGINE_MEM_DELETE(scope);
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, code_cstr, code_len);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = scope;
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_as_time_t(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	lua_pushinteger(L, iter ? static_cast<lua_Integer>(iter->AsTimeT()) : 0);
	return 1;
}

int l_bson_iter_as_timeval(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushinteger(L, 0);
		lua_pushinteger(L, 0);
		lua_pushnil(L);
		return 3;
	}
	struct timeval tv;
	iter->AsTimeval(&tv);
	lua_pushinteger(L, static_cast<lua_Integer>(tv.tv_sec));
	lua_pushinteger(L, static_cast<lua_Integer>(tv.tv_usec));
	lua_pushnil(L);
	return 3;
}

int l_bson_iter_binary_equal(lua_State* L) {
	auto* a = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	auto* b = GetUserdata<mongo::BsonIter>(L, 2, kMetaName);
	if (!a || !b) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, mongo::BsonIter::BinaryEqual(*a, *b));
	return 1;
}

int l_bson_iter_key_unsafe(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	if (!iter) {
		lua_pushnil(L);
		return 1;
	}
	const char* key = iter->KeyUnsafe();
	if (key)
		lua_pushstring(L, key);
	else
		lua_pushnil(L);
	return 1;
}

int l_bson_iter_init_from_data_at_offset(lua_State* L) {
	auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
	size_t len;
	const char* data_str = luaL_checklstring(L, 2, &len);
	auto offset = CheckIntegerArg<uint32_t>(L, 3);
	auto keylen = CheckIntegerArg<uint32_t>(L, 4);
	if (!iter) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L,
					iter->InitFromDataAtOffset(
						reinterpret_cast<const uint8_t*>(data_str), len, offset, keylen));
	return 1;
}

const luaL_Reg kLib[] = {
	{"iter_new", l_bson_iter_new},
	{"iter_next", l_bson_iter_next},
	{"iter_find", l_bson_iter_find},
	{"iter_key", l_bson_iter_key},
	{"iter_type", l_bson_iter_type},
	{"iter_as_int32", l_bson_iter_as_int32},
	{"iter_as_int64", l_bson_iter_as_int64},
	{"iter_as_double", l_bson_iter_as_double},
	{"iter_as_utf8", l_bson_iter_as_utf8},
	{"iter_as_bool", l_bson_iter_as_bool},
	{"iter_as_datetime", l_bson_iter_as_datetime},
	{"iter_as_oid", l_bson_iter_as_oid},
	{"iter_recurse", l_bson_iter_recurse},
	{"iter_as_binary", l_bson_iter_as_binary},
	{"iter_offset", l_bson_iter_offset},
	{"iter_as_document", l_bson_iter_as_document},
	{"iter_as_array", l_bson_iter_as_array},
	{"iter_as_timestamp", l_bson_iter_as_timestamp},
	{"iter_as_time_t", l_bson_iter_as_time_t},
	{"iter_as_timeval", l_bson_iter_as_timeval},
	{"iter_as_int64_coerce", l_bson_iter_as_int64_coerce},
	{"iter_as_double_coerce", l_bson_iter_as_double_coerce},
	{"iter_as_code", l_bson_iter_as_code},
	{"iter_as_code_with_scope", l_bson_iter_as_code_with_scope},
	{"iter_as_regex", l_bson_iter_as_regex},
	{"iter_as_symbol", l_bson_iter_as_symbol},
	{"iter_as_decimal128", l_bson_iter_as_decimal128},
	{"iter_find_case", l_bson_iter_find_case},
	{"iter_find_descendant", l_bson_iter_find_descendant},
	{"iter_find_wlen", l_bson_iter_find_wlen},
	{"iter_init_find", l_bson_iter_init_find},
	{"iter_init_find_case", l_bson_iter_init_find_case},
	{"iter_init_find_wlen", l_bson_iter_init_find_wlen},
	{"iter_init_from_data", l_bson_iter_init_from_data},
	{"iter_key_len", l_bson_iter_key_len},
	{"iter_dup_utf8", l_bson_iter_dup_utf8},
	{"iter_binary_subtype", l_bson_iter_binary_subtype},
	{"iter_as_bool_coerce", l_bson_iter_as_bool_coerce},
	{"iter_as_dbref", l_bson_iter_as_dbref},
	{"iter_value", l_bson_iter_value},
	{"iter_overwrite_int32", l_bson_iter_overwrite_int32},
	{"iter_overwrite_int64", l_bson_iter_overwrite_int64},
	{"iter_overwrite_double", l_bson_iter_overwrite_double},
	{"iter_overwrite_bool", l_bson_iter_overwrite_bool},
	{"iter_overwrite_datetime", l_bson_iter_overwrite_datetime},
	{"iter_overwrite_timestamp", l_bson_iter_overwrite_timestamp},
	{"iter_overwrite_oid", l_bson_iter_overwrite_oid},
	{"iter_overwrite_decimal128", l_bson_iter_overwrite_decimal128},
	{"iter_overwrite_binary", l_bson_iter_overwrite_binary},
	{"iter_binary_equal", l_bson_iter_binary_equal},
	{"iter_key_unsafe", l_bson_iter_key_unsafe},
	{"iter_init_from_data_at_offset", l_bson_iter_init_from_data_at_offset},
	{nullptr, nullptr},
};

}  // namespace

void RegisterBsonIterMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bson_iter_gc);
}

const luaL_Reg* GetBsonIterLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
