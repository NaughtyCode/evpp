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
#include "runtime/database/mongo_bind/bind_bulk_write_insert_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_update_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_update_many_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_replace_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_delete_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_delete_many_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_result.h"
#include "runtime/database/mongo_bind/bind_bulk_write_exception.h"
#include "runtime/database/mongo_bind/bind_server_api.h"
#include "runtime/database/mongo_bind/bind_find_and_modify_opts.h"
#include "runtime/database/mongo_bind/bind_error.h"
#include "runtime/database/mongo_bind/bind_host_list.h"
#include "runtime/database/mongo_bind/bind_ssl.h"
#include "runtime/database/mongo_bind/bind_gridfs.h"
#include "runtime/database/mongo_bind/bind_index_model.h"
#include "runtime/database/mongo_bind/bind_topology.h"
#include "runtime/database/mongo_bind/bind_bson_vector.h"
#include "runtime/database/mongo_bind/bind_bson_ext.h"
#include "runtime/database/mongo_bind/bind_stream.h"
#include "runtime/database/mongo_bind/bind_socket.h"
#include "runtime/database/mongo_bind/bind_log.h"
#include "runtime/database/mongo_bind/bind_oidc.h"
#include "runtime/database/mongo_bind/bind_encryption.h"
#include "runtime/database/mongo_bind/bind_apm.h"
#include "runtime/database/mongo_bind/bind_misc.h"
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
    RegisterMongoBulkWriteInsertOneOptsMeta(L);
    RegisterMongoBulkWriteUpdateOneOptsMeta(L);
    RegisterMongoBulkWriteUpdateManyOptsMeta(L);
    RegisterMongoBulkWriteReplaceOneOptsMeta(L);
    RegisterMongoBulkWriteDeleteOneOptsMeta(L);
    RegisterMongoBulkWriteDeleteManyOptsMeta(L);
    RegisterMongoBulkWriteResultMeta(L);
    RegisterMongoBulkWriteExceptionMeta(L);
    RegisterMongoServerApiMeta(L);
    RegisterMongoFindAndModifyOptsMeta(L);
    RegisterMongoErrorMeta(L);
    RegisterMongoHostListMeta(L);
    RegisterMongoSslOptsMeta(L);
    RegisterMongoGridFsFileOptsMeta(L);
    RegisterMongoGridFsFileMeta(L);
    RegisterMongoGridFsFileListMeta(L);
    RegisterMongoGridFsMeta(L);
    RegisterMongoGridFsBucketMeta(L);
    RegisterMongoIndexModelMeta(L);
    RegisterMongoServerDescriptionMeta(L);
    RegisterMongoTopologyDescriptionMeta(L);
    RegisterBsonVectorInt8ConstViewMeta(L);
    RegisterBsonVectorInt8ViewMeta(L);
    RegisterBsonVectorFloat32ConstViewMeta(L);
    RegisterBsonVectorFloat32ViewMeta(L);
    RegisterBsonVectorPackedBitConstViewMeta(L);
    RegisterBsonVectorPackedBitViewMeta(L);
    RegisterBsonContextMeta(L);
    RegisterBsonStringMeta(L);
    RegisterBsonJsonReaderMeta(L);
    RegisterBsonJsonDataReaderMeta(L);
    RegisterBsonReaderMeta(L);
    RegisterBsonWriterMeta(L);
    RegisterBsonJsonOptsMeta(L);
    RegisterBsonValueMeta(L);
    RegisterMongoStreamMeta(L);
    RegisterMongoSocketMeta(L);
    RegisterMongoStructuredLogOptsMeta(L);
    RegisterMongoStructuredLogEntryMeta(L);
    RegisterMongoOidcCredentialMeta(L);
    RegisterMongoOidcCallbackParamsMeta(L);
    RegisterMongoOidcCallbackMeta(L);
    RegisterMongoAutoEncryptionOptsMeta(L);
    RegisterMongoClientEncryptionOptsMeta(L);
    RegisterMongoClientEncryptionEncryptOptsMeta(L);
    RegisterMongoClientEncryptionEncryptRangeOptsMeta(L);
    RegisterMongoClientEncryptionEncryptTextPrefixOptsMeta(L);
    RegisterMongoClientEncryptionEncryptTextSuffixOptsMeta(L);
    RegisterMongoClientEncryptionEncryptTextSubstringOptsMeta(L);
    RegisterMongoClientEncryptionEncryptTextOptsMeta(L);
    RegisterMongoClientEncryptionDatakeyOptsMeta(L);
    RegisterMongoClientEncryptionRewrapManyDatakeyResultMeta(L);
    RegisterMongoClientEncryptionMeta(L);
    RegisterMongoApmCommandStartedEventMeta(L);
    RegisterMongoApmCommandSucceededEventMeta(L);
    RegisterMongoApmCommandFailedEventMeta(L);
    RegisterMongoApmServerChangedEventMeta(L);
    RegisterMongoApmServerOpeningEventMeta(L);
    RegisterMongoApmServerClosedEventMeta(L);
    RegisterMongoApmTopologyChangedEventMeta(L);
    RegisterMongoApmTopologyOpeningEventMeta(L);
    RegisterMongoApmTopologyClosedEventMeta(L);
    RegisterMongoApmServerHeartbeatStartedEventMeta(L);
    RegisterMongoApmServerHeartbeatSucceededEventMeta(L);
    RegisterMongoApmServerHeartbeatFailedEventMeta(L);
    RegisterMongoApmCallbacksMeta(L);
    RegisterMongoOptionalMeta(L);

    // ── Build "bson" module ───────────────────────────────────────────
    BeginModule(L);
    AddToModule(L, GetBsonDocumentLib());
    AddToModule(L, GetBsonIterLib());
    AddToModule(L, GetOidLib());
    AddToModule(L, GetBsonArrayBuilderLib());
    AddToModule(L, GetBsonContextLib());
    AddToModule(L, GetBsonStringLib());
    AddToModule(L, GetBsonJsonReaderLib());
    AddToModule(L, GetBsonJsonDataReaderLib());
    AddToModule(L, GetBsonReaderLib());
    AddToModule(L, GetBsonWriterLib());
    AddToModule(L, GetBsonJsonOptsLib());
    AddToModule(L, GetBsonValueLib());
    AddToModule(L, GetBsonVectorInt8ConstViewLib());
    AddToModule(L, GetBsonVectorInt8ViewLib());
    AddToModule(L, GetBsonVectorFloat32ConstViewLib());
    AddToModule(L, GetBsonVectorFloat32ViewLib());
    AddToModule(L, GetBsonVectorPackedBitConstViewLib());
    AddToModule(L, GetBsonVectorPackedBitViewLib());
    AddToModule(L, GetBsonExtLib());
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
    AddToModule(L, GetMongoBulkWriteInsertOneOptsLib());
    AddToModule(L, GetMongoBulkWriteUpdateOneOptsLib());
    AddToModule(L, GetMongoBulkWriteUpdateManyOptsLib());
    AddToModule(L, GetMongoBulkWriteReplaceOneOptsLib());
    AddToModule(L, GetMongoBulkWriteDeleteOneOptsLib());
    AddToModule(L, GetMongoBulkWriteDeleteManyOptsLib());
    AddToModule(L, GetMongoBulkWriteResultLib());
    AddToModule(L, GetMongoBulkWriteExceptionLib());
    AddToModule(L, GetMongoServerApiLib());
    AddToModule(L, GetMongoFindAndModifyOptsLib());
    AddToModule(L, GetMongoErrorLib());
    AddToModule(L, GetMongoHostListLib());
    AddToModule(L, GetMongoSslOptsLib());
    AddToModule(L, GetMongoGridFsFileOptsLib());
    AddToModule(L, GetMongoGridFsFileLib());
    AddToModule(L, GetMongoGridFsFileListLib());
    AddToModule(L, GetMongoGridFsLib());
    AddToModule(L, GetMongoGridFsBucketLib());
    AddToModule(L, GetMongoIndexModelLib());
    AddToModule(L, GetMongoServerDescriptionLib());
    AddToModule(L, GetMongoTopologyDescriptionLib());
    AddToModule(L, GetMongoStreamLib());
    AddToModule(L, GetMongoSocketLib());
    AddToModule(L, GetMongoStructuredLogOptsLib());
    AddToModule(L, GetMongoStructuredLogEntryLib());
    AddToModule(L, GetMongoLogLib());
    AddToModule(L, GetMongoOidcCredentialLib());
    AddToModule(L, GetMongoOidcCallbackParamsLib());
    AddToModule(L, GetMongoOidcCallbackLib());
    AddToModule(L, GetMongoAutoEncryptionOptsLib());
    AddToModule(L, GetMongoClientEncryptionOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptRangeOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptTextPrefixOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptTextSuffixOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptTextSubstringOptsLib());
    AddToModule(L, GetMongoClientEncryptionEncryptTextOptsLib());
    AddToModule(L, GetMongoClientEncryptionDatakeyOptsLib());
    AddToModule(L, GetMongoClientEncryptionRewrapManyDatakeyResultLib());
    AddToModule(L, GetMongoClientEncryptionLib());
    AddToModule(L, GetMongoApmCommandStartedEventLib());
    AddToModule(L, GetMongoApmCommandSucceededEventLib());
    AddToModule(L, GetMongoApmCommandFailedEventLib());
    AddToModule(L, GetMongoApmServerChangedEventLib());
    AddToModule(L, GetMongoApmServerOpeningEventLib());
    AddToModule(L, GetMongoApmServerClosedEventLib());
    AddToModule(L, GetMongoApmTopologyChangedEventLib());
    AddToModule(L, GetMongoApmTopologyOpeningEventLib());
    AddToModule(L, GetMongoApmTopologyClosedEventLib());
    AddToModule(L, GetMongoApmServerHeartbeatStartedEventLib());
    AddToModule(L, GetMongoApmServerHeartbeatSucceededEventLib());
    AddToModule(L, GetMongoApmServerHeartbeatFailedEventLib());
    AddToModule(L, GetMongoApmCallbacksLib());
    AddToModule(L, GetMongoOptionalLib());
    AddToModule(L, GetMongoMiscLib());
    EndModule(L, "mongoc");

    ENGINE_LOG_INFO(GetLogger(), "[mongo] Lua bindings registered ({} types)", 82);
}

} // namespace script
} // namespace engine

#endif
