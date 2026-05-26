#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/mongo_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/database/mongo_bind/bind_util.h"
#include "runtime/database/mongo_bind/bind_bson_document.h"
#include "runtime/database/mongo_bind/bind_bson_iter.h"
#include "runtime/database/mongo_bind/bind_oid.h"
#include "runtime/database/mongo_bind/bind_array_builder.h"
#include "runtime/database/mongo_bind/bind_uri.h"
#include "runtime/database/mongo_bind/bind_client.h"
#include "runtime/database/mongo_bind/bind_database.h"
#include "runtime/database/mongo_bind/bind_collection.h"
#include "runtime/database/mongo_bind/bind_cursor.h"
#include "runtime/database/mongo_bind/bind_client_pool.h"
#include "runtime/database/mongo_bind/bind_session.h"
#include "runtime/database/mongo_bind/bind_transaction_opts.h"
#include "runtime/database/mongo_bind/bind_session_opts.h"
#include "runtime/database/mongo_bind/bind_change_stream.h"
#include "runtime/database/mongo_bind/bind_read_prefs.h"
#include "runtime/database/mongo_bind/bind_write_concern.h"
#include "runtime/database/mongo_bind/bind_read_concern.h"
#include "runtime/database/mongo_bind/bind_bulk_operation.h"
#include "runtime/database/mongo_bind/bind_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_bulk_write_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_result.h"
#include "runtime/database/mongo_bind/bind_bulk_write_exception.h"
#include "runtime/database/mongo_bind/bind_server_api.h"
#include "runtime/database/mongo_bind/bind_find_and_modify_opts.h"
#include "runtime/database/mongo_bind/bind_error.h"
#include "runtime/database/mongo_bind/bind_host_list.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ExportMongo(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    // ── Register all metatables ───────────────────────────────────────
    RegisterBsonDocumentMeta(L);
    RegisterBsonIterMeta(L);
    RegisterBsonArrayBuilderMeta(L);
    RegisterMongoUriMeta(L);
    RegisterMongoClientMeta(L);
    RegisterMongoDatabaseMeta(L);
    RegisterMongoCollectionMeta(L);
    RegisterMongoCursorMeta(L);
    RegisterMongoClientPoolMeta(L);
    RegisterMongoSessionMeta(L);
    RegisterMongoTransactionOptsMeta(L);
    RegisterMongoSessionOptsMeta(L);
    RegisterMongoChangeStreamMeta(L);
    RegisterMongoReadPrefsMeta(L);
    RegisterMongoWriteConcernMeta(L);
    RegisterMongoReadConcernMeta(L);
    RegisterMongoBulkOperationMeta(L);
    RegisterMongoBulkWriteMeta(L);
    RegisterMongoBulkWriteOptsMeta(L);
    RegisterMongoBulkWriteResultMeta(L);
    RegisterMongoBulkWriteExceptionMeta(L);
    RegisterMongoServerApiMeta(L);
    RegisterMongoFindAndModifyOptsMeta(L);
    RegisterMongoErrorMeta(L);
    RegisterMongoHostListMeta(L);

    // ── Build "bson" module ───────────────────────────────────────────
    BeginModule(L);
    AddToModule(L, GetBsonDocumentLib());
    AddToModule(L, GetBsonIterLib());
    AddToModule(L, GetOidLib());
    AddToModule(L, GetBsonArrayBuilderLib());
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
    AddToModule(L, GetMongoTransactionOptsLib());
    AddToModule(L, GetMongoSessionOptsLib());
    AddToModule(L, GetMongoChangeStreamLib());
    AddToModule(L, GetMongoReadPrefsLib());
    AddToModule(L, GetMongoWriteConcernLib());
    AddToModule(L, GetMongoReadConcernLib());
    AddToModule(L, GetMongoBulkOperationLib());
    AddToModule(L, GetMongoBulkWriteLib());
    AddToModule(L, GetMongoBulkWriteOptsLib());
    AddToModule(L, GetMongoBulkWriteResultLib());
    AddToModule(L, GetMongoBulkWriteExceptionLib());
    AddToModule(L, GetMongoServerApiLib());
    AddToModule(L, GetMongoFindAndModifyOptsLib());
    AddToModule(L, GetMongoErrorLib());
    AddToModule(L, GetMongoHostListLib());
    EndModule(L, "mongoc");

    ENGINE_LOG_INFO(GetLogger(), "[mongo] Lua bindings registered ({} types)", 26);
}

} // namespace script
} // namespace engine

#endif
