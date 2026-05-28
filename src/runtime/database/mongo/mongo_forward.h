#pragma once

#include "runtime/core/mem/mem.h"

#if defined(ENGINE_MONGODB_ENABLED)


// Forward declarations for all mongo wrapper types.
// No mongo-c-driver headers included here — this header is safe to
// include anywhere in the project.

#ifdef _MSC_VER
#include <BaseTsd.h>
#if !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T_) && !defined(_SSIZE_T)
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#else
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#include <sys/types.h>
#endif
#endif

namespace engine {
namespace mongo {

class BsonDocument;
class BsonIter;
class BsonArrayBuilder;
class MongoDecimal128;
class MongoOid;
class MongoError;
class MongoUri;
class MongoClient;
class MongoDatabase;
class MongoCollection;
class MongoCursor;
class MongoBulkOperation;
class MongoSession;
class MongoTransactionOpts;
class MongoSessionOpts;
class MongoChangeStream;
class MongoClientPool;
class MongoFindAndModifyOpts;
class MongoIndexModel;
class MongoServerApi;
class MongoReadPrefs;
class MongoWriteConcern;
class MongoReadConcern;
class MongoSystem;
class MongoBulkWrite;
class MongoBulkWriteOpts;
class MongoBulkWriteResult;
class MongoBulkWriteException;
class MongoBulkWriteInsertOneOpts;
class MongoBulkWriteUpdateOneOpts;
class MongoBulkWriteUpdateManyOpts;
class MongoBulkWriteReplaceOneOpts;
class MongoBulkWriteDeleteOneOpts;
class MongoBulkWriteDeleteManyOpts;
struct MongoBulkWriteReturn;
struct MongoBulkWriteCheckAcknowledged;
struct MongoBulkWriteServerId;
class MongoApmCallbacks;
class MongoGridFs;
class MongoGridFsBucket;
class MongoGridFsFile;
class MongoGridFsFileList;
class BsonVectorInt8ConstView;
class BsonVectorInt8View;
class BsonVectorFloat32ConstView;
class BsonVectorFloat32View;
class BsonVectorPackedBitConstView;
class BsonVectorPackedBitView;
class BsonContext;
class BsonString;
class BsonJsonReader;
class BsonJsonDataReader;
class BsonReader;
class BsonWriter;
class BsonJsonOpts;
class BsonValue;
class MongoStructuredLogOpts;
class MongoClientEncryption;
class MongoAutoEncryptionOpts;
class MongoClientEncryptionOpts;
class MongoClientEncryptionEncryptOpts;
class MongoClientEncryptionDatakeyOpts;
class MongoClientEncryptionRewrapManyDatakeyResult;
class MongoClientEncryptionEncryptRangeOpts;
class MongoClientEncryptionEncryptTextPrefixOpts;
class MongoClientEncryptionEncryptTextSuffixOpts;
class MongoClientEncryptionEncryptTextSubstringOpts;
class MongoClientEncryptionEncryptTextOpts;
class MongoSslOpts;
class MongoHostList;
class MongoOidcCallback;
class MongoOidcCallbackParams;
class MongoOidcCredential;
class MongoSocket;
class MongoServerDescription;
class MongoTopologyDescription;
class MongoStream;
struct MongoIovec;
struct MongoSocketPollFd;

}  // namespace mongo
}  // namespace engine

#endif
