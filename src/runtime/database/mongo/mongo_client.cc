#include "runtime/database/mongo/mongo_client.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo/mongo_uri.h"

namespace engine {
namespace mongo {

// Impl structs must be defined before any code that constructs or accesses
// the corresponding wrapper types. MongoCollection::Impl is needed by
// MongoDatabase and MongoClient methods.

struct MongoCollection::Impl {
    mongoc_collection_t* coll = nullptr;
};

struct MongoDatabase::Impl {
    mongoc_database_t* db = nullptr;
};

struct MongoClient::Impl {
    mongoc_client_t* client = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════
// MongoClient
// ═══════════════════════════════════════════════════════════════════════

MongoClient* MongoClient::New(const char* uri_string) {
    auto* c = new MongoClient();
    c->impl_ = std::make_unique<Impl>();
    c->impl_->client = mongoc_client_new(uri_string);
    if (!c->impl_->client) {
        delete c;
        return nullptr;
    }
    return c;
}

MongoClient* MongoClient::New(const MongoUri& uri) {
    auto* c = new MongoClient();
    c->impl_ = std::make_unique<Impl>();
    c->impl_->client = mongoc_client_new_from_uri(
        static_cast<const mongoc_uri_t*>(uri.RawUri()));
    if (!c->impl_->client) {
        delete c;
        return nullptr;
    }
    return c;
}

MongoClient::MongoClient() : impl_(std::make_unique<Impl>()) {}

MongoClient::~MongoClient() {
    Destroy();
}

void MongoClient::Destroy() {
    if (impl_ && impl_->client) {
        mongoc_client_destroy(impl_->client);
        impl_->client = nullptr;
    }
}

void MongoClient::SetSocketTimeoutMs(int32_t timeout_ms) {
    if (impl_ && impl_->client) {
        mongoc_client_set_sockettimeoutms(impl_->client, timeout_ms);
    }
}

void MongoClient::SetAppname(const char* appname) {
    if (impl_ && impl_->client) {
        mongoc_client_set_appname(impl_->client, appname);
    }
}

MongoDatabase* MongoClient::GetDatabase(const char* name) {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_database_t* db = mongoc_client_get_database(impl_->client, name);
    auto* result = new MongoDatabase();
    result->impl_ = std::make_unique<MongoDatabase::Impl>();
    result->impl_->db = db;
    return result;
}

MongoDatabase* MongoClient::GetDefaultDatabase() {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_database_t* db = mongoc_client_get_default_database(impl_->client);
    if (!db) return nullptr;
    auto* result = new MongoDatabase();
    result->impl_ = std::make_unique<MongoDatabase::Impl>();
    result->impl_->db = db;
    return result;
}

MongoCollection* MongoClient::GetCollection(const char* db_name, const char* coll_name) {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_collection_t* coll = mongoc_client_get_collection(impl_->client, db_name, coll_name);
    auto* result = new MongoCollection();
    result->impl_ = std::make_unique<MongoCollection::Impl>();
    result->impl_->coll = coll;
    return result;
}

bool MongoClient::CommandSimple(const char* db_name, const BsonDocument& command,
                                 const MongoReadPrefs* read_prefs,
                                 BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_command_simple(
        impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        static_cast<bson_t*>(reply->RawBson()),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoClient::RawClient() {
    return impl_ ? impl_->client : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoDatabase
// ═══════════════════════════════════════════════════════════════════════

MongoDatabase::MongoDatabase() : impl_(std::make_unique<Impl>()) {}

MongoDatabase::~MongoDatabase() {
    Destroy();
}

void MongoDatabase::Destroy() {
    if (impl_ && impl_->db) {
        mongoc_database_destroy(impl_->db);
        impl_->db = nullptr;
    }
}

const char* MongoDatabase::GetName() const {
    return impl_ && impl_->db ? mongoc_database_get_name(impl_->db) : nullptr;
}

MongoCollection* MongoDatabase::GetCollection(const char* name) {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_collection_t* coll = mongoc_database_get_collection(impl_->db, name);
    auto* result = new MongoCollection();
    result->impl_ = std::make_unique<MongoCollection::Impl>();
    result->impl_->coll = coll;
    return result;
}

MongoCollection* MongoDatabase::CreateCollection(const char* name,
                                                   const BsonDocument* options,
                                                   MongoError* error) {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_collection_t* coll = mongoc_database_create_collection(
        impl_->db, name,
        options ? static_cast<const bson_t*>(options->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!coll) return nullptr;
    auto* result = new MongoCollection();
    result->impl_ = std::make_unique<MongoCollection::Impl>();
    result->impl_->coll = coll;
    return result;
}

bool MongoDatabase::Drop(MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_drop(impl_->db,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::HasCollection(const char* name, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_has_collection(impl_->db, name,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::CommandSimple(const BsonDocument& command,
                                   const MongoReadPrefs* read_prefs,
                                   BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_command_simple(
        impl_->db,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        static_cast<bson_t*>(reply->RawBson()),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoDatabase::RawDatabase() {
    return impl_ ? impl_->db : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoCollection
// ═══════════════════════════════════════════════════════════════════════

MongoCollection::MongoCollection() : impl_(std::make_unique<Impl>()) {}

MongoCollection::~MongoCollection() {
    Destroy();
}

void MongoCollection::Destroy() {
    if (impl_ && impl_->coll) {
        mongoc_collection_destroy(impl_->coll);
        impl_->coll = nullptr;
    }
}

const char* MongoCollection::GetName() const {
    return impl_ && impl_->coll ? mongoc_collection_get_name(impl_->coll) : nullptr;
}

bool MongoCollection::InsertOne(const BsonDocument& document, const BsonDocument* opts,
                                 BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_insert_one(
        impl_->coll,
        static_cast<const bson_t*>(document.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoCollection::FindWithOpts(const BsonDocument& filter, const BsonDocument* opts,
                                            const MongoReadPrefs* read_prefs) {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_cursor_t* cursor = mongoc_collection_find_with_opts(
        impl_->coll,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

bool MongoCollection::UpdateOne(const BsonDocument& selector, const BsonDocument& update,
                                 const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_update_one(
        impl_->coll,
        static_cast<const bson_t*>(selector.RawBson()),
        static_cast<const bson_t*>(update.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::UpdateMany(const BsonDocument& selector, const BsonDocument& update,
                                  const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_update_many(
        impl_->coll,
        static_cast<const bson_t*>(selector.RawBson()),
        static_cast<const bson_t*>(update.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::ReplaceOne(const BsonDocument& selector, const BsonDocument& replacement,
                                  const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_replace_one(
        impl_->coll,
        static_cast<const bson_t*>(selector.RawBson()),
        static_cast<const bson_t*>(replacement.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::DeleteOne(const BsonDocument& selector, const BsonDocument* opts,
                                 BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_delete_one(
        impl_->coll,
        static_cast<const bson_t*>(selector.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::DeleteMany(const BsonDocument& selector, const BsonDocument* opts,
                                  BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_delete_many(
        impl_->coll,
        static_cast<const bson_t*>(selector.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

int64_t MongoCollection::CountDocuments(const BsonDocument& filter, const BsonDocument* opts,
                                         const MongoReadPrefs* read_prefs,
                                         BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return -1;
    return mongoc_collection_count_documents(
        impl_->coll,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::Drop(MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_drop(impl_->coll,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::CreateIndex(const BsonDocument& keys, const BsonDocument* opts,
                                   BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;

    mongoc_index_model_t* model = mongoc_index_model_new(
        static_cast<const bson_t*>(keys.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!model) return false;

    bool ok = mongoc_collection_create_indexes_with_opts(
        impl_->coll, &model, 1, nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);

    mongoc_index_model_destroy(model);
    return ok;
}

void* MongoCollection::RawCollection() {
    return impl_ ? impl_->coll : nullptr;
}

} // namespace mongo
} // namespace engine
