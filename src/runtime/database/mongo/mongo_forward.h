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

} // namespace mongo
} // namespace engine
