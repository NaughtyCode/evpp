#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_change_stream.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.change_stream";

int l_change_stream_gc(lua_State* L) {
    auto* stream = GetUserdata<mongo::MongoChangeStream>(L, 1, kMetaName);
    if (stream) stream->Destroy();
    delete stream;
    *CheckUserdata<mongo::MongoChangeStream>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_change_stream_destroy(lua_State* L) { l_change_stream_gc(L); return 0; }

int l_change_stream_next(lua_State* L) {
    auto* stream = GetUserdata<mongo::MongoChangeStream>(L, 1, kMetaName);
    if (!stream) { lua_pushboolean(L, false); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    bool ok = stream->Next(doc);
    if (!ok) {
        delete *ud;
        *ud = nullptr;
        lua_pop(L, 1);
        lua_pushboolean(L, false);
        return 1;
    }
    return 1;
}

int l_change_stream_get_resume_token(lua_State* L) {
    auto* stream = GetUserdata<mongo::MongoChangeStream>(L, 1, kMetaName);
    if (!stream) { lua_pushnil(L); return 1; }
    const void* raw = stream->GetResumeToken();
    if (!raw) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument(
        mongo::BsonDocument::NewFromData(static_cast<const uint8_t*>(static_cast<const bson_t*>(raw) ?
            bson_get_data(static_cast<const bson_t*>(raw)) : nullptr),
            raw ? static_cast<const bson_t*>(raw)->len : 0));
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_change_stream_error_document(lua_State* L) {
    auto* stream = GetUserdata<mongo::MongoChangeStream>(L, 1, kMetaName);
    if (!stream) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    const void* raw_doc = nullptr;
    bool has_err = stream->ErrorDocument(&error, &raw_doc);
    lua_pushboolean(L, has_err);
    if (has_err) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

const luaL_Reg kLib[] = {
    {"change_stream_destroy", l_change_stream_destroy},
    {"change_stream_next", l_change_stream_next},
    {"change_stream_error_document", l_change_stream_error_document},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoChangeStreamMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_change_stream_gc);
}

const luaL_Reg* GetMongoChangeStreamLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
