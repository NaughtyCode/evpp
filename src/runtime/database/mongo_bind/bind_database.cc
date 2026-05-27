#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_database.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.database";

int l_db_gc(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (db) db->Destroy();
	delete db;
	*CheckUserdata<mongo::MongoDatabase>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_db_destroy(lua_State* L) {
	l_db_gc(L);
	return 0;
}

int l_db_get_collection(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* name = luaL_checkstring(L, 2);
	if (!db) {
		lua_pushnil(L);
		return 1;
	}
	auto* coll = db->GetCollection(name);
	if (!coll) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoCollection>(L, "mongoc.collection");
	*ud = coll;
	return 1;
}

int l_db_copy(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = db->Copy();
	if (!copy) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoDatabase>(L, kMetaName);
	*ud = copy;
	return 1;
}

int l_db_get_name(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* name = db ? db->GetName() : nullptr;
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

int l_db_drop(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db) {
		lua_pushboolean(L, false);
		lua_pushnil(L);
		return 2;
	}
	mongo::MongoError error;
	bool ok = db->Drop(&error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 2;
}

int l_db_command_simple(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!db || !cmd) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = db->CommandSimple(*cmd, nullptr, &reply, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
		*ud = doc;
	}
	return 3;
}

int l_db_set_read_prefs(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
	if (db && prefs) db->SetReadPrefs(*prefs);
	return 0;
}

int l_db_set_write_concern(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
	if (db && concern) db->SetWriteConcern(*concern);
	return 0;
}

int l_db_set_read_concern(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
	if (db && concern) db->SetReadConcern(*concern);
	return 0;
}

int l_db_get_read_prefs(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db || !db->GetReadPrefs())
		lua_pushnil(L);
	else
		lua_pushlightuserdata(L, const_cast<void*>(db->GetReadPrefs()));
	return 1;
}

int l_db_get_write_concern(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db || !db->GetWriteConcern())
		lua_pushnil(L);
	else
		lua_pushlightuserdata(L, const_cast<void*>(db->GetWriteConcern()));
	return 1;
}

int l_db_get_read_concern(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db || !db->GetReadConcern())
		lua_pushnil(L);
	else
		lua_pushlightuserdata(L, const_cast<void*>(db->GetReadConcern()));
	return 1;
}

int l_db_watch(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* pipeline = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!db || !pipeline) {
		lua_pushnil(L);
		return 1;
	}
	auto* stream = db->Watch(*pipeline, opts);
	if (!stream) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoChangeStream>(L, "mongoc.change_stream");
	*ud = stream;
	return 1;
}

int l_db_get_collection_names(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoError error;
	char** names = db->GetCollectionNames(&error);
	if (!names) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		return 2;
	}
	lua_newtable(L);
	int i = 1;
	for (char** p = names; *p; ++p) {
		lua_pushstring(L, *p);
		lua_rawseti(L, -2, i++);
	}
	bson_strfreev(names);
	return 1;
}

int l_db_has_collection(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* name = luaL_checkstring(L, 2);
	if (!db) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, db->HasCollection(name, &error));
	return 1;
}

int l_db_command_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* prefs = lua_isnoneornil(L, 3)
					  ? nullptr
					  : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!db || !cmd) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = db->CommandWithOpts(*cmd, prefs, opts, &reply, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
		*ud = doc;
	}
	return 3;
}

int l_db_create_collection(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* name = luaL_checkstring(L, 2);
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!db || !name) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::MongoError error;
	auto* coll = db->CreateCollection(name, opts, &error);
	if (!coll) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoCollection>(L, "mongoc.collection");
	*ud = coll;
	return 1;
}

int l_db_aggregate(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* pipeline = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!db || !pipeline) {
		lua_pushnil(L);
		return 1;
	}
	auto* cursor = db->Aggregate(*pipeline, opts, nullptr);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
	*ud = cursor;
	return 1;
}

int l_db_drop_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* opts =
		lua_isnoneornil(L, 2) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!db) {
		lua_pushboolean(L, false);
		lua_pushnil(L);
		return 2;
	}
	mongo::MongoError error;
	bool ok = db->DropWithOpts(opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 2;
}

int l_db_read_command_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* prefs = lua_isnoneornil(L, 3)
					  ? nullptr
					  : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!db || !cmd) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = db->ReadCommandWithOpts(*cmd, prefs, opts, &reply, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
		*ud = doc;
	}
	return 3;
}

int l_db_write_command_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!db || !cmd) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = db->WriteCommandWithOpts(*cmd, opts, &reply, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
		*ud = doc;
	}
	return 3;
}

int l_db_get_collection_names_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* opts =
		lua_isnoneornil(L, 2) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!db) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoError error;
	char** names = db->GetCollectionNamesWithOpts(opts, &error);
	if (!names) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		return 2;
	}
	lua_newtable(L);
	int i = 1;
	for (char** p = names; *p; ++p) {
		lua_pushstring(L, *p);
		lua_rawseti(L, -2, i++);
	}
	bson_strfreev(names);
	return 1;
}

int l_db_find_collections_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* opts =
		lua_isnoneornil(L, 2) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!db) {
		lua_pushnil(L);
		return 1;
	}
	auto* cursor = db->FindCollectionsWithOpts(opts);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
	*ud = cursor;
	return 1;
}

int l_db_read_write_command_with_opts(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* prefs = lua_isnoneornil(L, 3)
					  ? nullptr
					  : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!db || !cmd) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	bool ok = db->ReadWriteCommandWithOpts(*cmd, prefs, opts, &reply, &error);
	lua_pushboolean(L, ok);
	if (!ok) {
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
	} else {
		auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
		if (!doc) {
			lua_pushnil(L);
			lua_pushnil(L);
			return 3;
		}
		lua_pushnil(L);
		auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
		*ud = doc;
	}
	return 3;
}

// ── User management ────────────────────────────────────────────────────

int l_db_add_user(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* username = luaL_checkstring(L, 2);
	const char* password = luaL_checkstring(L, 3);
	auto* roles =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	auto* custom_data =
		lua_isnoneornil(L, 5) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 5, "bson.doc");
	if (!db || !username || !password) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		return 2;
	}
	mongo::MongoError error;
	bool ok = db->AddUser(username, password, roles, custom_data, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 2;
}

int l_db_remove_user(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	const char* username = luaL_checkstring(L, 2);
	if (!db) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, db->RemoveUser(username, &error));
	return 1;
}

int l_db_remove_all_users(lua_State* L) {
	auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
	if (!db) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, db->RemoveAllUsers(&error));
	return 1;
}

const luaL_Reg kLib[] = {
	{"db_destroy", l_db_destroy},
	{"db_get_collection", l_db_get_collection},
	{"db_create_collection", l_db_create_collection},
	{"db_aggregate", l_db_aggregate},
	{"db_copy", l_db_copy},
	{"db_get_name", l_db_get_name},
	{"db_drop", l_db_drop},
	{"db_command_simple", l_db_command_simple},
	{"db_set_read_prefs", l_db_set_read_prefs},
	{"db_set_write_concern", l_db_set_write_concern},
	{"db_set_read_concern", l_db_set_read_concern},
	{"db_get_read_prefs", l_db_get_read_prefs},
	{"db_get_write_concern", l_db_get_write_concern},
	{"db_get_read_concern", l_db_get_read_concern},
	{"db_watch", l_db_watch},
	{"db_get_collection_names", l_db_get_collection_names},
	{"db_has_collection", l_db_has_collection},
	{"db_command_with_opts", l_db_command_with_opts},
	{"db_drop_with_opts", l_db_drop_with_opts},
	{"db_read_command_with_opts", l_db_read_command_with_opts},
	{"db_write_command_with_opts", l_db_write_command_with_opts},
	{"db_read_write_command_with_opts", l_db_read_write_command_with_opts},
	{"db_get_collection_names_with_opts", l_db_get_collection_names_with_opts},
	{"db_find_collections_with_opts", l_db_find_collections_with_opts},
	{"db_add_user", l_db_add_user},
	{"db_remove_user", l_db_remove_user},
	{"db_remove_all_users", l_db_remove_all_users},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoDatabaseMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_db_gc);
}

const luaL_Reg* GetMongoDatabaseLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
