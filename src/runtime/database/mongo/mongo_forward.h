#pragma once

// Forward declarations for all mongo wrapper types.
// No mongo-c-driver headers included here — this header is safe to
// include anywhere in the project.

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
class BsonContext;
class BsonString;
class BsonJsonReader;
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

} // namespace mongo
} // namespace engine
