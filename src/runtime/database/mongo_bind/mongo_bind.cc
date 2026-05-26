#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/mongo_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/database/mongo_bind/bind_util.h"
#include "runtime/database/mongo_bind/bind_bson_document.h"
#include "runtime/database/mongo_bind/bind_bson_iter.h"
#include "runtime/database/mongo_bind/bind_oid.h"
#include "runtime/database/mongo_bind/bind_uri.h"
#include "runtime/database/mongo_bind/bind_client.h"
#include "runtime/database/mongo_bind/bind_database.h"
#include "runtime/database/mongo_bind/bind_collection.h"
#include "runtime/database/mongo_bind/bind_cursor.h"
#include "runtime/database/mongo_bind/bind_client_pool.h"
#include "runtime/database/mongo_bind/bind_session.h"
#include "runtime/database/mongo_bind/bind_change_stream.h"
#include "runtime/database/mongo_bind/bind_read_prefs.h"
#include "runtime/database/mongo_bind/bind_write_concern.h"
#include "runtime/database/mongo_bind/bind_read_concern.h"
#include "runtime/database/mongo_bind/bind_bulk_operation.h"
#include "runtime/database/mongo_bind/bind_bulkwrite.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ExportMongo(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    // ── Register all metatables ───────────────────────────────────────
    RegisterBsonDocumentMeta(L);
    RegisterBsonIterMeta(L);
    RegisterMongoUriMeta(L);
    RegisterMongoClientMeta(L);
    RegisterMongoDatabaseMeta(L);
    RegisterMongoCollectionMeta(L);
    RegisterMongoCursorMeta(L);
    RegisterMongoClientPoolMeta(L);
    RegisterMongoSessionMeta(L);
    RegisterMongoChangeStreamMeta(L);
    RegisterMongoReadPrefsMeta(L);
    RegisterMongoWriteConcernMeta(L);
    RegisterMongoReadConcernMeta(L);
    RegisterMongoBulkOperationMeta(L);
    RegisterMongoBulkWriteMeta(L);

    // ── Build "bson" module ───────────────────────────────────────────
    BeginModule(L);
    AddToModule(L, GetBsonDocumentLib());
    AddToModule(L, GetBsonIterLib());
    AddToModule(L, GetOidLib());
    EndModule(L, "bson");

    // ── Build "mongoc" module ─────────────────────────────────────────
    BeginModule(L);
    AddToModule(L, GetMongoUriLib());
    AddToModule(L, GetMongoClientLib());
    AddToModule(L, GetMongoDatabaseLib());
    AddToModule(L, GetMongoCollectionLib());
    AddToModule(L, GetMongoCursorLib());
    AddToModule(L, GetMongoClientPoolLib());
    AddToModule(L, GetMongoSessionLib());
    AddToModule(L, GetMongoChangeStreamLib());
    AddToModule(L, GetMongoReadPrefsLib());
    AddToModule(L, GetMongoWriteConcernLib());
    AddToModule(L, GetMongoReadConcernLib());
    AddToModule(L, GetMongoBulkOperationLib());
    AddToModule(L, GetMongoBulkWriteLib());
    EndModule(L, "mongoc");

    ENGINE_LOG_INFO(GetLogger(), "[mongo] Lua bindings registered ({} types)", 16);
}

} // namespace script
} // namespace engine

#endif
