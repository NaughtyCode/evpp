#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_topology.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_host_list.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo/mongo_topology.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// MongoServerDescription
// ═══════════════════════════════════════════════════════════════════════════

const char* kSdMeta = "mongoc.server_description";

int l_sd_gc(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	delete sd;
	*CheckUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta) = nullptr;
	return 0;
}

int l_sd_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw server_description pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* sd = new (std::nothrow) mongo::MongoServerDescription(raw);
	if (!sd) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoServerDescription>(L, kSdMeta);
	*ud = sd;
	return 1;
}

int l_sd_destroy(lua_State* L) {
	l_sd_gc(L);
	return 0;
}

int l_sd_new_copy(lua_State* L) {
	auto* other = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	if (!other) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = mongo::MongoServerDescription::NewCopy(other);
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "copy failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoServerDescription>(L, kSdMeta);
	*ud = copy;
	return 1;
}

int l_sd_id(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	lua_pushinteger(L, sd ? sd->Id() : 0);
	return 1;
}

int l_sd_host(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	if (!sd) {
		lua_pushnil(L);
		return 1;
	}
	const auto* host = sd->Host();
	if (!host) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, host->GetHostAndPort());
	return 1;
}

int l_sd_last_update_time(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	lua_pushinteger(L, sd ? sd->LastUpdateTime() : 0);
	return 1;
}

int l_sd_round_trip_time(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	lua_pushinteger(L, sd ? sd->RoundTripTime() : 0);
	return 1;
}

int l_sd_type(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	const char* t = sd ? sd->Type() : nullptr;
	if (t)
		lua_pushstring(L, t);
	else
		lua_pushnil(L);
	return 1;
}

int l_sd_hello_response(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	if (!sd) {
		lua_pushnil(L);
		return 1;
	}
	const auto* raw_bson = static_cast<const bson_t*>(sd->HelloResponse());
	if (!raw_bson) {
		lua_pushnil(L);
		return 1;
	}
	const uint8_t* data = bson_get_data(raw_bson);
	uint32_t len = raw_bson->len;
	auto* doc = new (std::nothrow) mongo::BsonDocument(data, len);
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

int l_sd_compressor_id(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	lua_pushinteger(L, sd ? sd->CompressorId() : -1);
	return 1;
}

int l_sd_get_raw(lua_State* L) {
	auto* sd = GetUserdata<mongo::MongoServerDescription>(L, 1, kSdMeta);
	if (!sd) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, sd->Raw());
	return 1;
}

const luaL_Reg kSdLib[] = {
	{"server_desc_new", l_sd_new},
	{"server_desc_destroy", l_sd_destroy},
	{"server_desc_new_copy", l_sd_new_copy},
	{"server_desc_id", l_sd_id},
	{"server_desc_host", l_sd_host},
	{"server_desc_last_update_time", l_sd_last_update_time},
	{"server_desc_round_trip_time", l_sd_round_trip_time},
	{"server_desc_type", l_sd_type},
	{"server_desc_hello_response", l_sd_hello_response},
	{"server_desc_compressor_id", l_sd_compressor_id},
	{"server_desc_get_raw", l_sd_get_raw},
	{nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoTopologyDescription
// ═══════════════════════════════════════════════════════════════════════════

const char* kTdMeta = "mongoc.topology_description";

int l_td_gc(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	delete td;
	*CheckUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta) = nullptr;
	return 0;
}

int l_td_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw topology_description pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* td = new (std::nothrow) mongo::MongoTopologyDescription(raw);
	if (!td) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoTopologyDescription>(L, kTdMeta);
	*ud = td;
	return 1;
}

int l_td_destroy(lua_State* L) {
	l_td_gc(L);
	return 0;
}

int l_td_new_copy(lua_State* L) {
	auto* other = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	if (!other) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = mongo::MongoTopologyDescription::NewCopy(other);
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "copy failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoTopologyDescription>(L, kTdMeta);
	*ud = copy;
	return 1;
}

int l_td_has_readable_server(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	auto* prefs = lua_isnoneornil(L, 2)
					  ? nullptr
					  : GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
	lua_pushboolean(L, td && td->HasReadableServer(prefs));
	return 1;
}

int l_td_has_writable_server(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	lua_pushboolean(L, td && td->HasWritableServer());
	return 1;
}

int l_td_type(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	const char* t = td ? td->Type() : nullptr;
	if (t)
		lua_pushstring(L, t);
	else
		lua_pushnil(L);
	return 1;
}

int l_td_servers(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	if (!td) {
		lua_pushnil(L);
		return 1;
	}
	size_t n;
	auto** servers = td->GetServers(&n);
	if (!servers) {
		lua_pushnil(L);
		return 1;
	}
	lua_createtable(L, (int) n, 0);
	for (size_t i = 0; i < n; ++i) {
		auto** ud = NewUserdata<mongo::MongoServerDescription>(L, kSdMeta);
		*ud = servers[i];
		lua_rawseti(L, -2, (int) i + 1);
	}
	bson_free(servers);
	return 1;
}

int l_td_get_raw(lua_State* L) {
	auto* td = GetUserdata<mongo::MongoTopologyDescription>(L, 1, kTdMeta);
	if (!td) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, td->Raw());
	return 1;
}

const luaL_Reg kTdLib[] = {
	{"topology_desc_new", l_td_new},
	{"topology_desc_destroy", l_td_destroy},
	{"topology_desc_new_copy", l_td_new_copy},
	{"topology_desc_has_readable_server", l_td_has_readable_server},
	{"topology_desc_has_writable_server", l_td_has_writable_server},
	{"topology_desc_type", l_td_type},
	{"topology_desc_servers", l_td_servers},
	{"topology_desc_get_raw", l_td_get_raw},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoServerDescriptionMeta(lua_State* L) {
	RegisterMetatable(L, kSdMeta, nullptr, l_sd_gc);
}

void RegisterMongoTopologyDescriptionMeta(lua_State* L) {
	RegisterMetatable(L, kTdMeta, nullptr, l_td_gc);
}

const luaL_Reg* GetMongoServerDescriptionLib() {
	return kSdLib;
}
const luaL_Reg* GetMongoTopologyDescriptionLib() {
	return kTdLib;
}

}  // namespace script
}  // namespace engine

#endif
