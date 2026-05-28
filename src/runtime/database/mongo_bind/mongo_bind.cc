#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/mongo_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/database/mongo_bind/bind_apm.h"
#include "runtime/database/mongo_bind/bind_array_builder.h"
#include "runtime/database/mongo_bind/bind_bson_document.h"
#include "runtime/database/mongo_bind/bind_bson_ext.h"
#include "runtime/database/mongo_bind/bind_bson_iter.h"
#include "runtime/database/mongo_bind/bind_bson_vector.h"
#include "runtime/database/mongo_bind/bind_bulk_operation.h"
#include "runtime/database/mongo_bind/bind_bulk_write_delete_many_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_delete_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_exception.h"
#include "runtime/database/mongo_bind/bind_bulk_write_insert_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_replace_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_result.h"
#include "runtime/database/mongo_bind/bind_bulk_write_update_many_opts.h"
#include "runtime/database/mongo_bind/bind_bulk_write_update_one_opts.h"
#include "runtime/database/mongo_bind/bind_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_change_stream.h"
#include "runtime/database/mongo_bind/bind_client.h"
#include "runtime/database/mongo_bind/bind_client_pool.h"
#include "runtime/database/mongo_bind/bind_collection.h"
#include "runtime/database/mongo_bind/bind_cursor.h"
#include "runtime/database/mongo_bind/bind_database.h"
#include "runtime/database/mongo_bind/bind_encryption.h"
#include "runtime/database/mongo_bind/bind_error.h"
#include "runtime/database/mongo_bind/bind_find_and_modify_opts.h"
#include "runtime/database/mongo_bind/bind_gridfs.h"
#include "runtime/database/mongo_bind/bind_host_list.h"
#include "runtime/database/mongo_bind/bind_index_model.h"
#include "runtime/database/mongo_bind/bind_log.h"
#include "runtime/database/mongo_bind/bind_misc.h"
#include "runtime/database/mongo_bind/bind_oid.h"
#include "runtime/database/mongo_bind/bind_oidc.h"
#include "runtime/database/mongo_bind/bind_read_concern.h"
#include "runtime/database/mongo_bind/bind_read_prefs.h"
#include "runtime/database/mongo_bind/bind_server_api.h"
#include "runtime/database/mongo_bind/bind_session.h"
#include "runtime/database/mongo_bind/bind_session_opts.h"
#include "runtime/database/mongo_bind/bind_socket.h"
#include "runtime/database/mongo_bind/bind_ssl.h"
#include "runtime/database/mongo_bind/bind_stream.h"
#include "runtime/database/mongo_bind/bind_topology.h"
#include "runtime/database/mongo_bind/bind_transaction_opts.h"
#include "runtime/database/mongo_bind/bind_uri.h"
#include "runtime/database/mongo_bind/bind_util.h"
#include "runtime/database/mongo_bind/bind_write_concern.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ExportMongo(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return;

	// ── Register all metatables (auto-generated from mongo_types.def) ──
	#define MONGOC_TYPE(module, CppName) Register##CppName##Meta(L);
	#include "runtime/database/mongo_bind/mongo_types.def"

	// ── Build "bson" module ───────────────────────────────────────────
	BeginModule(L);
	#define MONGOC_TYPE(module, CppName) \
		if (module[0] == 'b') AddToModule(L, Get##CppName##Lib());
	#include "runtime/database/mongo_bind/mongo_types.def"
	// Library-only types (no metatable)
	AddToModule(L, GetOidLib());
	AddToModule(L, GetBsonExtLib());
	EndModule(L, "bson");

	// ── Build "mongoc" module ─────────────────────────────────────────
	BeginModule(L);
	#define MONGOC_TYPE(module, CppName) \
		if (module[0] == 'm') AddToModule(L, Get##CppName##Lib());
	#include "runtime/database/mongo_bind/mongo_types.def"
	// Library-only types (no metatable)
	AddToModule(L, GetMongoLogLib());
	AddToModule(L, GetMongoMiscLib());
	EndModule(L, "mongoc");

	ENGINE_LOG_INFO(GetLogger(), "[mongo] Lua bindings registered ({} types)", 82);
}

}  // namespace script
}  // namespace engine

#endif
