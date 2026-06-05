#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_encryption.h"

#include <new>
#include <vector>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_encryption.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

// 1. MongoAutoEncryptionOpts
const char* kAeoMeta = "mongoc.auto_encryption_opts";

int l_auto_encrypt_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta) = nullptr;
	return 0;
}

int l_auto_encrypt_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoAutoEncryptionOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoAutoEncryptionOpts>(L, kAeoMeta);
	*ud = opts;
	return 1;
}

int l_auto_encrypt_opts_destroy(lua_State* L) {
	l_auto_encrypt_opts_gc(L);
	return 0;
}

int l_auto_encrypt_opts_set_keyvault_client(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	void* client = lua_touserdata(L, 2);
	if (opts) opts->SetKeyvaultClient(client);
	return 0;
}

int l_auto_encrypt_opts_set_keyvault_client_pool(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	void* pool = lua_touserdata(L, 2);
	if (opts) opts->SetKeyvaultClientPool(pool);
	return 0;
}

int l_auto_encrypt_opts_set_keyvault_namespace(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	const char* db = luaL_checkstring(L, 2);
	const char* coll = luaL_checkstring(L, 3);
	if (opts) opts->SetKeyvaultNamespace(db, coll);
	return 0;
}

int l_auto_encrypt_opts_set_kms_providers(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto* providers = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && providers) opts->SetKmsProviders(*providers);
	return 0;
}

int l_auto_encrypt_opts_set_key_expiration(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto ms = CheckIntegerArg<uint64_t>(L, 2);
	if (opts) opts->SetKeyExpiration(ms);
	return 0;
}

int l_auto_encrypt_opts_set_tls_opts(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto* tls = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && tls) opts->SetTlsOpts(*tls);
	return 0;
}

int l_auto_encrypt_opts_set_schema_map(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto* schema_map = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && schema_map) opts->SetSchemaMap(*schema_map);
	return 0;
}

int l_auto_encrypt_opts_set_encrypted_fields_map(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto* efields = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && efields) opts->SetEncryptedFieldsMap(*efields);
	return 0;
}

int l_auto_encrypt_opts_set_bypass_auto_encryption(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	bool bypass = lua_toboolean(L, 2) != 0;
	if (opts) opts->SetBypassAutoEncryption(bypass);
	return 0;
}

int l_auto_encrypt_opts_set_bypass_query_analysis(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	bool bypass = lua_toboolean(L, 2) != 0;
	if (opts) opts->SetBypassQueryAnalysis(bypass);
	return 0;
}

int l_auto_encrypt_opts_set_extra(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	auto* extra = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && extra) opts->SetExtra(*extra);
	return 0;
}

int l_auto_encrypt_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoAutoEncryptionOpts>(L, 1, kAeoMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kAeoLib[] = {
	{"auto_encrypt_opts_new", l_auto_encrypt_opts_new},
	{"auto_encrypt_opts_destroy", l_auto_encrypt_opts_destroy},
	{"auto_encrypt_opts_set_keyvault_client", l_auto_encrypt_opts_set_keyvault_client},
	{"auto_encrypt_opts_set_keyvault_client_pool", l_auto_encrypt_opts_set_keyvault_client_pool},
	{"auto_encrypt_opts_set_keyvault_namespace", l_auto_encrypt_opts_set_keyvault_namespace},
	{"auto_encrypt_opts_set_kms_providers", l_auto_encrypt_opts_set_kms_providers},
	{"auto_encrypt_opts_set_key_expiration", l_auto_encrypt_opts_set_key_expiration},
	{"auto_encrypt_opts_set_tls_opts", l_auto_encrypt_opts_set_tls_opts},
	{"auto_encrypt_opts_set_schema_map", l_auto_encrypt_opts_set_schema_map},
	{"auto_encrypt_opts_set_encrypted_fields_map", l_auto_encrypt_opts_set_encrypted_fields_map},
	{"auto_encrypt_opts_set_bypass_auto_encryption",
	 l_auto_encrypt_opts_set_bypass_auto_encryption},
	{"auto_encrypt_opts_set_bypass_query_analysis", l_auto_encrypt_opts_set_bypass_query_analysis},
	{"auto_encrypt_opts_set_extra", l_auto_encrypt_opts_set_extra},
	{"auto_encrypt_opts_get_raw", l_auto_encrypt_opts_get_raw},
	{nullptr, nullptr},
};

const char* kCeoMeta = "mongoc.client_encryption_opts";

int l_client_encrypt_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta) = nullptr;
	return 0;
}

int l_client_encrypt_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionOpts>(L, kCeoMeta);
	*ud = opts;
	return 1;
}

int l_client_encrypt_opts_destroy(lua_State* L) {
	l_client_encrypt_opts_gc(L);
	return 0;
}

int l_client_encrypt_opts_set_keyvault_client(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	void* client = lua_touserdata(L, 2);
	if (opts) opts->SetKeyvaultClient(client);
	return 0;
}

int l_client_encrypt_opts_set_keyvault_namespace(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	const char* db = luaL_checkstring(L, 2);
	const char* coll = luaL_checkstring(L, 3);
	if (opts) opts->SetKeyvaultNamespace(db, coll);
	return 0;
}

int l_client_encrypt_opts_set_kms_providers(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	auto* providers = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && providers) opts->SetKmsProviders(*providers);
	return 0;
}

int l_client_encrypt_opts_set_tls_opts(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	auto* tls = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && tls) opts->SetTlsOpts(*tls);
	return 0;
}

int l_client_encrypt_opts_set_key_expiration(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	auto ms = CheckIntegerArg<uint64_t>(L, 2);
	if (opts) opts->SetKeyExpiration(ms);
	return 0;
}

int l_client_encrypt_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, kCeoMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kCeoLib[] = {
	{"client_encrypt_opts_new", l_client_encrypt_opts_new},
	{"client_encrypt_opts_destroy", l_client_encrypt_opts_destroy},
	{"client_encrypt_opts_set_keyvault_client", l_client_encrypt_opts_set_keyvault_client},
	{"client_encrypt_opts_set_keyvault_namespace", l_client_encrypt_opts_set_keyvault_namespace},
	{"client_encrypt_opts_set_kms_providers", l_client_encrypt_opts_set_kms_providers},
	{"client_encrypt_opts_set_tls_opts", l_client_encrypt_opts_set_tls_opts},
	{"client_encrypt_opts_set_key_expiration", l_client_encrypt_opts_set_key_expiration},
	{"client_encrypt_opts_get_raw", l_client_encrypt_opts_get_raw},
	{nullptr, nullptr},
};

const char* kEncMeta = "mongoc.encrypt_opts";

int l_encrypt_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta) = nullptr;
	return 0;
}

int l_encrypt_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, kEncMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_opts_destroy(lua_State* L) {
	l_encrypt_opts_gc(L);
	return 0;
}

int l_encrypt_opts_set_key_id(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	void* keyid = lua_touserdata(L, 2);
	if (opts) opts->SetKeyId(keyid);
	return 0;
}

int l_encrypt_opts_set_key_alt_name(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	const char* name = luaL_checkstring(L, 2);
	if (opts) opts->SetKeyAltName(name);
	return 0;
}

int l_encrypt_opts_set_algorithm(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	const char* algorithm = luaL_checkstring(L, 2);
	if (opts) opts->SetAlgorithm(algorithm);
	return 0;
}

int l_encrypt_opts_set_contention_factor(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	auto factor = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (opts) opts->SetContentionFactor(factor);
	return 0;
}

int l_encrypt_opts_set_query_type(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	const char* query_type = luaL_checkstring(L, 2);
	if (opts) opts->SetQueryType(query_type);
	return 0;
}

int l_encrypt_opts_set_range_opts(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	auto* range_opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(
		L, 2, "mongoc.encrypt_range_opts");
	if (opts && range_opts) opts->SetRangeOpts(range_opts->Raw());
	return 0;
}

int l_encrypt_opts_set_text_opts(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	auto* text_opts =
		GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 2, "mongoc.encrypt_text_opts");
	if (opts && text_opts) opts->SetTextOpts(text_opts->Raw());
	return 0;
}

int l_encrypt_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 1, kEncMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kEncLib[] = {
	{"encrypt_opts_new", l_encrypt_opts_new},
	{"encrypt_opts_destroy", l_encrypt_opts_destroy},
	{"encrypt_opts_set_key_id", l_encrypt_opts_set_key_id},
	{"encrypt_opts_set_key_alt_name", l_encrypt_opts_set_key_alt_name},
	{"encrypt_opts_set_algorithm", l_encrypt_opts_set_algorithm},
	{"encrypt_opts_set_contention_factor", l_encrypt_opts_set_contention_factor},
	{"encrypt_opts_set_query_type", l_encrypt_opts_set_query_type},
	{"encrypt_opts_set_range_opts", l_encrypt_opts_set_range_opts},
	{"encrypt_opts_set_text_opts", l_encrypt_opts_set_text_opts},
	{"encrypt_opts_get_raw", l_encrypt_opts_get_raw},
	{nullptr, nullptr},
};

const char* kErMeta = "mongoc.encrypt_range_opts";

int l_encrypt_range_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta) = nullptr;
	return 0;
}

int l_encrypt_range_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptRangeOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, kErMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_range_opts_destroy(lua_State* L) {
	l_encrypt_range_opts_gc(L);
	return 0;
}

int l_encrypt_range_opts_set_trim_factor(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetTrimFactor(val);
	return 0;
}

int l_encrypt_range_opts_set_sparsity(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (opts) opts->SetSparsity(val);
	return 0;
}

int l_encrypt_range_opts_set_min(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	void* val = lua_touserdata(L, 2);
	if (opts) opts->SetMin(val);
	return 0;
}

int l_encrypt_range_opts_set_max(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	void* val = lua_touserdata(L, 2);
	if (opts) opts->SetMax(val);
	return 0;
}

int l_encrypt_range_opts_set_precision(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetPrecision(val);
	return 0;
}

int l_encrypt_range_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptRangeOpts>(L, 1, kErMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kErLib[] = {
	{"encrypt_range_opts_new", l_encrypt_range_opts_new},
	{"encrypt_range_opts_destroy", l_encrypt_range_opts_destroy},
	{"encrypt_range_opts_set_trim_factor", l_encrypt_range_opts_set_trim_factor},
	{"encrypt_range_opts_set_sparsity", l_encrypt_range_opts_set_sparsity},
	{"encrypt_range_opts_set_min", l_encrypt_range_opts_set_min},
	{"encrypt_range_opts_set_max", l_encrypt_range_opts_set_max},
	{"encrypt_range_opts_set_precision", l_encrypt_range_opts_set_precision},
	{"encrypt_range_opts_get_raw", l_encrypt_range_opts_get_raw},
	{nullptr, nullptr},
};

const char* kTpMeta = "mongoc.encrypt_text_prefix_opts";

int l_encrypt_text_prefix_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, 1, kTpMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, 1, kTpMeta) = nullptr;
	return 0;
}

int l_encrypt_text_prefix_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptTextPrefixOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, kTpMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_text_prefix_opts_destroy(lua_State* L) {
	l_encrypt_text_prefix_opts_gc(L);
	return 0;
}

int l_encrypt_text_prefix_opts_set_max_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, 1, kTpMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMaxQueryLength(val);
	return 0;
}

int l_encrypt_text_prefix_opts_set_min_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, 1, kTpMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMinQueryLength(val);
	return 0;
}

int l_encrypt_text_prefix_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(L, 1, kTpMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kTpLib[] = {
	{"encrypt_text_prefix_opts_new", l_encrypt_text_prefix_opts_new},
	{"encrypt_text_prefix_opts_destroy", l_encrypt_text_prefix_opts_destroy},
	{"encrypt_text_prefix_opts_set_max_query_length",
	 l_encrypt_text_prefix_opts_set_max_query_length},
	{"encrypt_text_prefix_opts_set_min_query_length",
	 l_encrypt_text_prefix_opts_set_min_query_length},
	{"encrypt_text_prefix_opts_get_raw", l_encrypt_text_prefix_opts_get_raw},
	{nullptr, nullptr},
};

const char* kTsMeta = "mongoc.encrypt_text_suffix_opts";

int l_encrypt_text_suffix_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, 1, kTsMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, 1, kTsMeta) = nullptr;
	return 0;
}

int l_encrypt_text_suffix_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptTextSuffixOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, kTsMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_text_suffix_opts_destroy(lua_State* L) {
	l_encrypt_text_suffix_opts_gc(L);
	return 0;
}

int l_encrypt_text_suffix_opts_set_max_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, 1, kTsMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMaxQueryLength(val);
	return 0;
}

int l_encrypt_text_suffix_opts_set_min_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, 1, kTsMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMinQueryLength(val);
	return 0;
}

int l_encrypt_text_suffix_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(L, 1, kTsMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kTsLib[] = {
	{"encrypt_text_suffix_opts_new", l_encrypt_text_suffix_opts_new},
	{"encrypt_text_suffix_opts_destroy", l_encrypt_text_suffix_opts_destroy},
	{"encrypt_text_suffix_opts_set_max_query_length",
	 l_encrypt_text_suffix_opts_set_max_query_length},
	{"encrypt_text_suffix_opts_set_min_query_length",
	 l_encrypt_text_suffix_opts_set_min_query_length},
	{"encrypt_text_suffix_opts_get_raw", l_encrypt_text_suffix_opts_get_raw},
	{nullptr, nullptr},
};

const char* kTssMeta = "mongoc.encrypt_text_substring_opts";

int l_encrypt_text_substring_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta) = nullptr;
	return 0;
}

int l_encrypt_text_substring_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptTextSubstringOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, kTssMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_text_substring_opts_destroy(lua_State* L) {
	l_encrypt_text_substring_opts_gc(L);
	return 0;
}

int l_encrypt_text_substring_opts_set_max_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMaxLength(val);
	return 0;
}

int l_encrypt_text_substring_opts_set_max_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMaxQueryLength(val);
	return 0;
}

int l_encrypt_text_substring_opts_set_min_query_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta);
	auto val = CheckIntegerArg<int32_t>(L, 2);
	if (opts) opts->SetStrMinQueryLength(val);
	return 0;
}

int l_encrypt_text_substring_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(L, 1, kTssMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kTssLib[] = {
	{"encrypt_text_substring_opts_new", l_encrypt_text_substring_opts_new},
	{"encrypt_text_substring_opts_destroy", l_encrypt_text_substring_opts_destroy},
	{"encrypt_text_substring_opts_set_max_length", l_encrypt_text_substring_opts_set_max_length},
	{"encrypt_text_substring_opts_set_max_query_length",
	 l_encrypt_text_substring_opts_set_max_query_length},
	{"encrypt_text_substring_opts_set_min_query_length",
	 l_encrypt_text_substring_opts_set_min_query_length},
	{"encrypt_text_substring_opts_get_raw", l_encrypt_text_substring_opts_get_raw},
	{nullptr, nullptr},
};

const char* kTxtMeta = "mongoc.encrypt_text_opts";

int l_encrypt_text_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta) = nullptr;
	return 0;
}

int l_encrypt_text_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionEncryptTextOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, kTxtMeta);
	*ud = opts;
	return 1;
}

int l_encrypt_text_opts_destroy(lua_State* L) {
	l_encrypt_text_opts_gc(L);
	return 0;
}

int l_encrypt_text_opts_set_prefix(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	auto* prefix = lua_isnoneornil(L, 2)
					   ? nullptr
					   : GetUserdata<mongo::MongoClientEncryptionEncryptTextPrefixOpts>(
							 L, 2, "mongoc.encrypt_text_prefix_opts");
	if (opts && prefix) opts->SetPrefix(prefix->Raw());
	return 0;
}

int l_encrypt_text_opts_set_suffix(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	auto* suffix = lua_isnoneornil(L, 2)
					   ? nullptr
					   : GetUserdata<mongo::MongoClientEncryptionEncryptTextSuffixOpts>(
							 L, 2, "mongoc.encrypt_text_suffix_opts");
	if (opts && suffix) opts->SetSuffix(suffix->Raw());
	return 0;
}

int l_encrypt_text_opts_set_substring(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	auto* sub = lua_isnoneornil(L, 2)
					? nullptr
					: GetUserdata<mongo::MongoClientEncryptionEncryptTextSubstringOpts>(
						  L, 2, "mongoc.encrypt_text_substring_opts");
	if (opts && sub) opts->SetSubstring(sub->Raw());
	return 0;
}

int l_encrypt_text_opts_set_case_sensitive(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	bool val = lua_toboolean(L, 2) != 0;
	if (opts) opts->SetCaseSensitive(val);
	return 0;
}

int l_encrypt_text_opts_set_diacritic_sensitive(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	bool val = lua_toboolean(L, 2) != 0;
	if (opts) opts->SetDiacriticSensitive(val);
	return 0;
}

int l_encrypt_text_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionEncryptTextOpts>(L, 1, kTxtMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kTxtLib[] = {
	{"encrypt_text_opts_new", l_encrypt_text_opts_new},
	{"encrypt_text_opts_destroy", l_encrypt_text_opts_destroy},
	{"encrypt_text_opts_set_prefix", l_encrypt_text_opts_set_prefix},
	{"encrypt_text_opts_set_suffix", l_encrypt_text_opts_set_suffix},
	{"encrypt_text_opts_set_substring", l_encrypt_text_opts_set_substring},
	{"encrypt_text_opts_set_case_sensitive", l_encrypt_text_opts_set_case_sensitive},
	{"encrypt_text_opts_set_diacritic_sensitive", l_encrypt_text_opts_set_diacritic_sensitive},
	{"encrypt_text_opts_get_raw", l_encrypt_text_opts_get_raw},
	{nullptr, nullptr},
};

const char* kDkMeta = "mongoc.datakey_opts";

int l_datakey_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta) = nullptr;
	return 0;
}

int l_datakey_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionDatakeyOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, kDkMeta);
	*ud = opts;
	return 1;
}

int l_datakey_opts_destroy(lua_State* L) {
	l_datakey_opts_gc(L);
	return 0;
}

int l_datakey_opts_set_masterkey(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta);
	auto* mk = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && mk) opts->SetMasterkey(*mk);
	return 0;
}

int l_datakey_opts_set_key_alt_names(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta);
	if (!lua_istable(L, 2) || !opts) return 0;
	auto n = CheckLengthArg<uint32_t>(L, 2);
	std::vector<char*> names(n);
	for (uint32_t i = 0; i < n; ++i) {
		lua_rawgeti(L, 2, i + 1);
		names[i] = const_cast<char*>(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	opts->SetKeyAltNames(names.data(), n);
	return 0;
}

int l_datakey_opts_set_key_material(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta);
	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);
	if (opts)
		opts->SetKeyMaterial(reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len));
	return 0;
}

int l_datakey_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 1, kDkMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kDkLib[] = {
	{"datakey_opts_new", l_datakey_opts_new},
	{"datakey_opts_destroy", l_datakey_opts_destroy},
	{"datakey_opts_set_masterkey", l_datakey_opts_set_masterkey},
	{"datakey_opts_set_key_alt_names", l_datakey_opts_set_key_alt_names},
	{"datakey_opts_set_key_material", l_datakey_opts_set_key_material},
	{"datakey_opts_get_raw", l_datakey_opts_get_raw},
	{nullptr, nullptr},
};

const char* kRwrMeta = "mongoc.rewrap_result";

int l_rewrap_result_gc(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, 1, kRwrMeta);
	CLOUDENGINE_MEM_DELETE(result);
	*CheckUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, 1, kRwrMeta) = nullptr;
	return 0;
}

int l_rewrap_result_new(lua_State* L) {
	auto* result = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionRewrapManyDatakeyResult);
	if (!result) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, kRwrMeta);
	*ud = result;
	return 1;
}

int l_rewrap_result_destroy(lua_State* L) {
	l_rewrap_result_gc(L);
	return 0;
}

int l_rewrap_result_get_bulk_write_result(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, 1, kRwrMeta);
	if (!result) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(result->GetBulkWriteResult());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, mongo::BsonDocument::NewFromData(bson_get_data(raw), raw->len));
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

int l_rewrap_result_get_raw(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, 1, kRwrMeta);
	if (!result) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = result->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kRwrLib[] = {
	{"rewrap_result_new", l_rewrap_result_new},
	{"rewrap_result_destroy", l_rewrap_result_destroy},
	{"rewrap_result_get_bulk_write_result", l_rewrap_result_get_bulk_write_result},
	{"rewrap_result_get_raw", l_rewrap_result_get_raw},
	{nullptr, nullptr},
};

const char* kCeMeta = "mongoc.client_encryption";

int l_client_encryption_gc(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	if (enc) enc->Destroy();
	*CheckUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta) = nullptr;
	return 0;
}

int l_client_encryption_new(lua_State* L) {
	auto* opts =
		GetUserdata<mongo::MongoClientEncryptionOpts>(L, 1, "mongoc.client_encryption_opts");
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid opts");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* enc = mongo::MongoClientEncryption::New(opts, &error);
	if (!enc) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientEncryption>(L, kCeMeta);
	*ud = enc;
	return 1;
}

int l_client_encryption_destroy(lua_State* L) {
	l_client_encryption_gc(L);
	return 0;
}

int l_client_encryption_create_datakey(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	const char* provider = luaL_checkstring(L, 2);
	auto* opts = lua_isnoneornil(L, 3)
					 ? nullptr
					 : GetUserdata<mongo::MongoClientEncryptionDatakeyOpts>(L, 3, kDkMeta);
	void* keyid_out = lua_touserdata(L, 4);
	mongo::MongoError error;
	bool ok = enc && enc->CreateDatakey(provider, opts, keyid_out, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_client_encryption_rewrap_many_datakey(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	const char* provider = luaL_checkstring(L, 3);
	auto* master_key =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!enc || !filter) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoClientEncryptionRewrapManyDatakeyResult result;
	mongo::MongoError error;
	bool ok = enc->RewrapManyDatakey(*filter, provider, master_key, &result, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		lua_pushnil(L);
		auto* copy = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoClientEncryptionRewrapManyDatakeyResult, std::move(result));
		if (!copy) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		auto** ud = NewUserdata<mongo::MongoClientEncryptionRewrapManyDatakeyResult>(L, kRwrMeta);
		*ud = copy;
	}
	return 3;
}

// Helper to create a BsonDocument userdata from an operation that fills a reply
bool push_key_management_reply(lua_State* L,
							   const char* meta,
							   bool ok,
							   mongo::BsonDocument& reply,
							   const mongo::MongoError& error) {
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return false;  // caller must handle
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, meta);
		*ud = doc;
	}
	return true;
}

int l_client_encryption_delete_key(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* keyid = lua_touserdata(L, 2);
	if (!enc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid encryption");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = enc->DeleteKey(keyid, &reply, &error);
	push_key_management_reply(L, "bson.doc", ok, reply, error);
	return 3;
}

int l_client_encryption_get_key(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* keyid = lua_touserdata(L, 2);
	if (!enc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid encryption");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = enc->GetKey(keyid, &reply, &error);
	push_key_management_reply(L, "bson.doc", ok, reply, error);
	return 3;
}

int l_client_encryption_get_keys(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	if (!enc) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoError error;
	auto* cursor = enc->GetKeys(&error);
	if (!cursor) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
	*ud = cursor;
	return 1;
}

int l_client_encryption_add_key_alt_name(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* keyid = lua_touserdata(L, 2);
	const char* altname = luaL_checkstring(L, 3);
	if (!enc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid encryption");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = enc->AddKeyAltName(keyid, altname, &reply, &error);
	push_key_management_reply(L, "bson.doc", ok, reply, error);
	return 3;
}

int l_client_encryption_remove_key_alt_name(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* keyid = lua_touserdata(L, 2);
	const char* altname = luaL_checkstring(L, 3);
	if (!enc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid encryption");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = enc->RemoveKeyAltName(keyid, altname, &reply, &error);
	push_key_management_reply(L, "bson.doc", ok, reply, error);
	return 3;
}

int l_client_encryption_get_key_by_alt_name(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	const char* altname = luaL_checkstring(L, 2);
	if (!enc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid encryption");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = enc->GetKeyByAltName(altname, &reply, &error);
	push_key_management_reply(L, "bson.doc", ok, reply, error);
	return 3;
}

int l_client_encryption_encrypt(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* value = lua_touserdata(L, 2);
	auto* opts = lua_isnoneornil(L, 3)
					 ? nullptr
					 : GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 3, kEncMeta);
	void* ciphertext_out = lua_touserdata(L, 4);
	mongo::MongoError error;
	bool ok = enc && enc->Encrypt(value, opts, ciphertext_out, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_client_encryption_encrypt_expression(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	auto* expr = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts = lua_isnoneornil(L, 3)
					 ? nullptr
					 : GetUserdata<mongo::MongoClientEncryptionEncryptOpts>(L, 3, kEncMeta);
	if (!enc || !expr) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument expr_out;
	mongo::MongoError error;
	bool ok = enc->EncryptExpression(*expr, opts, &expr_out, &error);
	push_key_management_reply(L, "bson.doc", ok, expr_out, error);
	return 3;
}

int l_client_encryption_decrypt(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* ciphertext = lua_touserdata(L, 2);
	void* value_out = lua_touserdata(L, 3);
	mongo::MongoError error;
	bool ok = enc && enc->Decrypt(ciphertext, value_out, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_client_encryption_create_encrypted_collection(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	void* database = lua_touserdata(L, 2);
	const char* name = luaL_checkstring(L, 3);
	auto* in_opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	auto* out_opts = GetUserdata<mongo::BsonDocument>(L, 5, "bson.doc");
	const char* kms_provider = luaL_checkstring(L, 6);
	auto* masterkey =
		lua_isnoneornil(L, 7) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 7, "bson.doc");
	if (!enc || !out_opts) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	void* coll = enc->CreateEncryptedCollection(
		database, name, in_opts, out_opts, kms_provider, masterkey, &error);
	if (!coll) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	lua_pushlightuserdata(L, coll);
	return 1;
}

int l_client_encryption_get_crypt_shared_version(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	if (!enc) {
		lua_pushnil(L);
		return 1;
	}
	const char* version = enc->GetCryptSharedVersion();
	if (version)
		lua_pushstring(L, version);
	else
		lua_pushnil(L);
	return 1;
}

int l_client_encryption_get_raw(lua_State* L) {
	auto* enc = GetUserdata<mongo::MongoClientEncryption>(L, 1, kCeMeta);
	if (!enc) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = enc->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kCeLib[] = {
	{"client_encryption_new", l_client_encryption_new},
	{"client_encryption_destroy", l_client_encryption_destroy},
	{"client_encryption_create_datakey", l_client_encryption_create_datakey},
	{"client_encryption_rewrap_many_datakey", l_client_encryption_rewrap_many_datakey},
	{"client_encryption_delete_key", l_client_encryption_delete_key},
	{"client_encryption_get_key", l_client_encryption_get_key},
	{"client_encryption_get_keys", l_client_encryption_get_keys},
	{"client_encryption_add_key_alt_name", l_client_encryption_add_key_alt_name},
	{"client_encryption_remove_key_alt_name", l_client_encryption_remove_key_alt_name},
	{"client_encryption_get_key_by_alt_name", l_client_encryption_get_key_by_alt_name},
	{"client_encryption_encrypt", l_client_encryption_encrypt},
	{"client_encryption_encrypt_expression", l_client_encryption_encrypt_expression},
	{"client_encryption_decrypt", l_client_encryption_decrypt},
	{"client_encryption_create_encrypted_collection",
	 l_client_encryption_create_encrypted_collection},
	{"client_encryption_get_crypt_shared_version", l_client_encryption_get_crypt_shared_version},
	{"client_encryption_get_raw", l_client_encryption_get_raw},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoAutoEncryptionOptsMeta(lua_State* L) {
	RegisterMetatable(L, kAeoMeta, nullptr, l_auto_encrypt_opts_gc);
}

void RegisterMongoClientEncryptionOptsMeta(lua_State* L) {
	RegisterMetatable(L, kCeoMeta, nullptr, l_client_encrypt_opts_gc);
}

void RegisterMongoClientEncryptionEncryptOptsMeta(lua_State* L) {
	RegisterMetatable(L, kEncMeta, nullptr, l_encrypt_opts_gc);
}

void RegisterMongoClientEncryptionEncryptRangeOptsMeta(lua_State* L) {
	RegisterMetatable(L, kErMeta, nullptr, l_encrypt_range_opts_gc);
}

void RegisterMongoClientEncryptionEncryptTextPrefixOptsMeta(lua_State* L) {
	RegisterMetatable(L, kTpMeta, nullptr, l_encrypt_text_prefix_opts_gc);
}

void RegisterMongoClientEncryptionEncryptTextSuffixOptsMeta(lua_State* L) {
	RegisterMetatable(L, kTsMeta, nullptr, l_encrypt_text_suffix_opts_gc);
}

void RegisterMongoClientEncryptionEncryptTextSubstringOptsMeta(lua_State* L) {
	RegisterMetatable(L, kTssMeta, nullptr, l_encrypt_text_substring_opts_gc);
}

void RegisterMongoClientEncryptionEncryptTextOptsMeta(lua_State* L) {
	RegisterMetatable(L, kTxtMeta, nullptr, l_encrypt_text_opts_gc);
}

void RegisterMongoClientEncryptionDatakeyOptsMeta(lua_State* L) {
	RegisterMetatable(L, kDkMeta, nullptr, l_datakey_opts_gc);
}

void RegisterMongoClientEncryptionRewrapManyDatakeyResultMeta(lua_State* L) {
	RegisterMetatable(L, kRwrMeta, nullptr, l_rewrap_result_gc);
}

void RegisterMongoClientEncryptionMeta(lua_State* L) {
	RegisterMetatable(L, kCeMeta, nullptr, l_client_encryption_gc);
}

const luaL_Reg* GetMongoAutoEncryptionOptsLib() {
	return kAeoLib;
}
const luaL_Reg* GetMongoClientEncryptionOptsLib() {
	return kCeoLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptOptsLib() {
	return kEncLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptRangeOptsLib() {
	return kErLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptTextPrefixOptsLib() {
	return kTpLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptTextSuffixOptsLib() {
	return kTsLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptTextSubstringOptsLib() {
	return kTssLib;
}
const luaL_Reg* GetMongoClientEncryptionEncryptTextOptsLib() {
	return kTxtLib;
}
const luaL_Reg* GetMongoClientEncryptionDatakeyOptsLib() {
	return kDkLib;
}
const luaL_Reg* GetMongoClientEncryptionRewrapManyDatakeyResultLib() {
	return kRwrLib;
}
const luaL_Reg* GetMongoClientEncryptionLib() {
	return kCeLib;
}

}  // namespace script
}  // namespace engine

#endif
