#pragma once

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error \
	"bson_table_codec.h is internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including this header."
#endif

#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

// Export db_bson:
//   db_bson.to_bson(table[, root_as_array|options]) -> bson.doc | nil, err
//   db_bson.from_table(table[, root_as_array|options]) -> bson.doc | nil, err
//   db_bson.to_table(bson_doc_or_raw_doc_wrapper[, root_as_array|options]) -> table | nil, err
//   db_bson.null / undefined / min_key / max_key sentinels
//   db_bson.array/document and BSON scalar wrapper constructors
CLOUD_ENGINE_API void ExportDbBsonCodec(ScriptVM& vm);

}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
