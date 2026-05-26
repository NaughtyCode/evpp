#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_client.h"

#include <mongoc/mongoc.h>

#include <cstdint>
#include <vector>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulk.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_find_and_modify_opts.h"
#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo/mongo_session.h"
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
    bool owned = true; // false when client belongs to a pool
};

// ═══════════════════════════════════════════════════════════════════════
// MongoClient
// ═══════════════════════════════════════════════════════════════════════

MongoClient* MongoClient::New(const char* uri_string) {
    auto* c = new MongoClient();
    c->impl_->client = mongoc_client_new(uri_string);
    if (!c->impl_->client) {
        delete c;
        return nullptr;
    }
    return c;
}

MongoClient* MongoClient::New(const MongoUri& uri) {
    auto* c = new MongoClient();
    c->impl_->client = mongoc_client_new_from_uri(
        static_cast<const mongoc_uri_t*>(uri.RawUri()));
    if (!c->impl_->client) {
        delete c;
        return nullptr;
    }
    return c;
}

MongoClient* MongoClient::New(const MongoUri& uri, MongoError* error) {
    auto* c = new MongoClient();
    c->impl_->client = mongoc_client_new_from_uri_with_error(
        static_cast<const mongoc_uri_t*>(uri.RawUri()),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
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
    if (impl_ && impl_->client && impl_->owned) {
        mongoc_client_destroy(impl_->client);
        impl_->client = nullptr;
    }
}

void MongoClient::ReleaseFromPool() {
    if (impl_) {
        impl_->owned = false;
        impl_->client = nullptr;
    }
}

MongoClient* MongoClient::FromPooled(void* raw_client) {
    auto* c = new MongoClient();
    c->impl_->client = static_cast<mongoc_client_t*>(raw_client);
    c->impl_->owned = false; // pooled clients must be returned to the pool, not destroyed
    return c;
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

bool MongoClient::SetServerApi(const MongoServerApi& api, MongoError* error) {
    return impl_ && impl_->client && mongoc_client_set_server_api(impl_->client,
        static_cast<const mongoc_server_api_t*>(api.RawServerApi()),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void MongoClient::SetSslOpts(const void* ssl_opts) {
    if (impl_ && impl_->client && ssl_opts) {
        mongoc_client_set_ssl_opts(impl_->client,
            static_cast<const mongoc_ssl_opt_t*>(ssl_opts));
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
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoClient::RawClient() {
    return impl_ ? impl_->client : nullptr;
}

// ── MongoClient: new methods ───────────────────────────────────────────

MongoUri MongoClient::GetUri() const {
    MongoUri result;
    if (impl_ && impl_->client) {
        const mongoc_uri_t* uri = mongoc_client_get_uri(impl_->client);
        if (uri) {
            result.SetRawUri(mongoc_uri_copy(uri));
        }
    }
    return result;
}

void MongoClient::SetReadPrefs(const MongoReadPrefs& read_prefs) {
    if (impl_ && impl_->client)
        mongoc_client_set_read_prefs(impl_->client,
            static_cast<const mongoc_read_prefs_t*>(read_prefs.RawReadPrefs()));
}

void MongoClient::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->client)
        mongoc_client_set_write_concern(impl_->client,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

void MongoClient::SetReadConcern(const MongoReadConcern& read_concern) {
    if (impl_ && impl_->client)
        mongoc_client_set_read_concern(impl_->client,
            static_cast<const mongoc_read_concern_t*>(read_concern.RawReadConcern()));
}

const void* MongoClient::GetReadPrefs() const {
    return impl_ && impl_->client ? mongoc_client_get_read_prefs(impl_->client) : nullptr;
}

const void* MongoClient::GetWriteConcern() const {
    return impl_ && impl_->client ? mongoc_client_get_write_concern(impl_->client) : nullptr;
}

const void* MongoClient::GetReadConcern() const {
    return impl_ && impl_->client ? mongoc_client_get_read_concern(impl_->client) : nullptr;
}

void MongoClient::SetErrorApi(uint32_t version) {
    if (impl_ && impl_->client && version <= static_cast<uint32_t>(INT32_MAX))
        mongoc_client_set_error_api(impl_->client, static_cast<int32_t>(version));
}

void MongoClient::Reset() {
    if (impl_ && impl_->client)
        mongoc_client_reset(impl_->client);
}

bool MongoClient::ReadCommandWithOpts(const char* db_name, const BsonDocument& command,
                                       const MongoReadPrefs* read_prefs, const BsonDocument* opts,
                                       BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_read_command_with_opts(impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClient::WriteCommandWithOpts(const char* db_name, const BsonDocument& command,
                                        const BsonDocument* opts,
                                        BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_write_command_with_opts(impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClient::ReadWriteCommandWithOpts(const char* db_name, const BsonDocument& command,
                                            const MongoReadPrefs* read_prefs, const BsonDocument* opts,
                                            BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_read_write_command_with_opts(impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoSession* MongoClient::StartSession(const MongoSessionOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_client_session_t* session = mongoc_client_start_session(impl_->client,
        opts ? static_cast<const mongoc_session_opt_t*>(opts->RawSessionOpts()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!session) return nullptr;
    auto* result = new MongoSession();
    result->SetRawSession(session);
    return result;
}

char** MongoClient::GetDatabaseNames(MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_get_database_names_with_opts(impl_->client, nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

char** MongoClient::GetDatabaseNamesWithOpts(const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_get_database_names_with_opts(impl_->client,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoClient::FindDatabasesWithOpts(const BsonDocument* opts) {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_cursor_t* cursor = mongoc_client_find_databases_with_opts(impl_->client,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

MongoChangeStream* MongoClient::Watch(const BsonDocument& pipeline, const BsonDocument* opts) {
    if (!impl_ || !impl_->client) return nullptr;
    mongoc_change_stream_t* stream = mongoc_client_watch(impl_->client,
        static_cast<const bson_t*>(pipeline.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!stream) return nullptr;
    auto* result = new MongoChangeStream();
    result->SetRawStream(stream);
    return result;
}

// ── MongoClient: additional methods (gap fill) ────────────────────────────

bool MongoClient::CommandSimpleWithServerId(const char* db_name, const BsonDocument& command,
                                             const MongoReadPrefs* read_prefs, uint32_t server_id,
                                             BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_command_simple_with_server_id(
        impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        server_id,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClient::CommandWithOpts(const char* db_name, const BsonDocument& command,
                                   const MongoReadPrefs* read_prefs, const BsonDocument* opts,
                                   BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->client) return false;
    return mongoc_client_command_with_opts(impl_->client, db_name,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClient::SetApmCallbacks(void* callbacks, void* context) {
    return impl_ && impl_->client && mongoc_client_set_apm_callbacks(
        impl_->client, static_cast<mongoc_apm_callbacks_t*>(callbacks), context);
}

bool MongoClient::SetStructuredLogOpts(const void* opts) {
    return impl_ && impl_->client && mongoc_client_set_structured_log_opts(
        impl_->client, static_cast<const mongoc_structured_log_opts_t*>(opts));
}

void* MongoClient::SelectServer(bool for_writes, const MongoReadPrefs* prefs, MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_select_server(impl_->client, for_writes,
        prefs ? static_cast<const mongoc_read_prefs_t*>(prefs->RawReadPrefs()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoClient::GetServerDescription(uint32_t server_id) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_get_server_description(impl_->client, server_id);
}

void** MongoClient::GetServerDescriptions(size_t* n) const {
    if (!impl_ || !impl_->client) return nullptr;
    return reinterpret_cast<void**>(
        mongoc_client_get_server_descriptions(impl_->client, n));
}

void MongoClient::ServerDescriptionsDestroyAll(void** sds, size_t n) {
    mongoc_server_descriptions_destroy_all(
        reinterpret_cast<mongoc_server_description_t**>(sds), n);
}

void* MongoClient::GetHandshakeDescription(uint32_t server_id, const BsonDocument* opts,
                                             MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_get_handshake_description(impl_->client, server_id,
        opts ? const_cast<bson_t*>(static_cast<const bson_t*>(opts->RawBson())) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClient::EnableAutoEncryption(void* opts, MongoError* error) {
    return impl_ && impl_->client && mongoc_client_enable_auto_encryption(
        impl_->client, static_cast<mongoc_auto_encryption_opts_t*>(opts),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

const char* MongoClient::GetCryptSharedVersion() const {
    return impl_ && impl_->client ? mongoc_client_get_crypt_shared_version(impl_->client) : nullptr;
}

void* MongoClient::GetGridfs(const char* db, const char* prefix, MongoError* error) {
    if (!impl_ || !impl_->client) return nullptr;
    return mongoc_client_get_gridfs(impl_->client, db, prefix,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void MongoClient::SetStreamInitiator(void* initiator, void* user_data) {
    if (impl_ && impl_->client)
        mongoc_client_set_stream_initiator(impl_->client,
            reinterpret_cast<mongoc_stream_initiator_t>(initiator), user_data);
}

bool MongoClient::SetOidcCallback(const void* callback) {
    return impl_ && impl_->client && mongoc_client_set_oidc_callback(
        impl_->client, static_cast<const mongoc_oidc_callback_t*>(callback));
}

bool MongoClient::AppendMetadata(const char* name, const char* version, const char* platform) {
    return impl_ && impl_->client && mongoc_client_append_metadata(
        impl_->client, name, version, platform);
}

void MongoClient::SetUsleepImpl(UsleepFunc func, void* user_data) {
    if (impl_ && impl_->client)
        mongoc_client_set_usleep_impl(impl_->client,
            reinterpret_cast<mongoc_usleep_func_t>(func), user_data);
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

MongoDatabase* MongoDatabase::Copy() const {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_database_t* db = mongoc_database_copy(impl_->db);
    if (!db) return nullptr;
    auto* result = new MongoDatabase();
    result->impl_ = std::make_unique<Impl>();
    result->impl_->db = db;
    return result;
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
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
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

MongoCollection* MongoCollection::Copy() const {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_collection_t* coll = mongoc_collection_copy(impl_->coll);
    if (!coll) return nullptr;
    auto* result = new MongoCollection();
    result->impl_ = std::make_unique<Impl>();
    result->impl_->coll = coll;
    return result;
}

bool MongoCollection::CommandSimple(const BsonDocument& command,
                                     const MongoReadPrefs* read_prefs,
                                     BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_command_simple(impl_->coll,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::CommandWithOpts(const BsonDocument& command,
                                       const MongoReadPrefs* read_prefs,
                                       const BsonDocument* opts,
                                       BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_command_with_opts(impl_->coll,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::ReadCommandWithOpts(const BsonDocument& command,
                                           const MongoReadPrefs* read_prefs,
                                           const BsonDocument* opts,
                                           BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_read_command_with_opts(impl_->coll,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::WriteCommandWithOpts(const BsonDocument& command,
                                            const BsonDocument* opts,
                                            BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_write_command_with_opts(impl_->coll,
        static_cast<const bson_t*>(command.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::ReadWriteCommandWithOpts(const BsonDocument& command,
                                                const MongoReadPrefs* read_prefs,
                                                const BsonDocument* opts,
                                                BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_read_write_command_with_opts(impl_->coll,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
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

// ── MongoDatabase: additional methods ───────────────────────────────────

MongoCursor* MongoDatabase::Aggregate(const BsonDocument& pipeline, const BsonDocument* opts,
                                       const MongoReadPrefs* read_prefs) {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_cursor_t* cursor = mongoc_database_aggregate(impl_->db,
        static_cast<const bson_t*>(pipeline.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

MongoChangeStream* MongoDatabase::Watch(const BsonDocument& pipeline, const BsonDocument* opts) {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_change_stream_t* stream = mongoc_database_watch(impl_->db,
        static_cast<const bson_t*>(pipeline.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!stream) return nullptr;
    auto* result = new MongoChangeStream();
    result->SetRawStream(stream);
    return result;
}

bool MongoDatabase::DropWithOpts(const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_drop_with_opts(impl_->db,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::ReadCommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                                         const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_read_command_with_opts(impl_->db,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::WriteCommandWithOpts(const BsonDocument& command, const BsonDocument* opts,
                                          BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_write_command_with_opts(impl_->db,
        static_cast<const bson_t*>(command.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::ReadWriteCommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                                              const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_read_write_command_with_opts(impl_->db,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::CommandWithOpts(const BsonDocument& command, const MongoReadPrefs* read_prefs,
                                     const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_command_with_opts(impl_->db,
        static_cast<const bson_t*>(command.RawBson()),
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

const void* MongoDatabase::GetReadPrefs() const {
    return impl_ && impl_->db ? mongoc_database_get_read_prefs(impl_->db) : nullptr;
}

void MongoDatabase::SetReadPrefs(const MongoReadPrefs& read_prefs) {
    if (impl_ && impl_->db)
        mongoc_database_set_read_prefs(impl_->db,
            static_cast<const mongoc_read_prefs_t*>(read_prefs.RawReadPrefs()));
}

const void* MongoDatabase::GetWriteConcern() const {
    return impl_ && impl_->db ? mongoc_database_get_write_concern(impl_->db) : nullptr;
}

void MongoDatabase::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->db)
        mongoc_database_set_write_concern(impl_->db,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

const void* MongoDatabase::GetReadConcern() const {
    return impl_ && impl_->db ? mongoc_database_get_read_concern(impl_->db) : nullptr;
}

void MongoDatabase::SetReadConcern(const MongoReadConcern& read_concern) {
    if (impl_ && impl_->db)
        mongoc_database_set_read_concern(impl_->db,
            static_cast<const mongoc_read_concern_t*>(read_concern.RawReadConcern()));
}

char** MongoDatabase::GetCollectionNames(MongoError* error) {
    if (!impl_ || !impl_->db) return nullptr;
    return mongoc_database_get_collection_names_with_opts(impl_->db, nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

char** MongoDatabase::GetCollectionNamesWithOpts(const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->db) return nullptr;
    return mongoc_database_get_collection_names_with_opts(impl_->db,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoDatabase::FindCollectionsWithOpts(const BsonDocument* opts) {
    if (!impl_ || !impl_->db) return nullptr;
    mongoc_cursor_t* cursor = mongoc_database_find_collections_with_opts(impl_->db,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

bool MongoDatabase::AddUser(const char* username, const char* password,
                             const BsonDocument* roles, const BsonDocument* custom_data,
                             MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_add_user(impl_->db, username, password,
        roles ? static_cast<const bson_t*>(roles->RawBson()) : nullptr,
        custom_data ? static_cast<const bson_t*>(custom_data->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::RemoveUser(const char* username, MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_remove_user(impl_->db, username,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoDatabase::RemoveAllUsers(MongoError* error) {
    if (!impl_ || !impl_->db) return false;
    return mongoc_database_remove_all_users(impl_->db,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

// ── MongoCollection: additional methods ──────────────────────────────────

const void* MongoCollection::GetReadPrefs() const {
    return impl_ && impl_->coll ? mongoc_collection_get_read_prefs(impl_->coll) : nullptr;
}

void MongoCollection::SetReadPrefs(const MongoReadPrefs& read_prefs) {
    if (impl_ && impl_->coll)
        mongoc_collection_set_read_prefs(impl_->coll,
            static_cast<const mongoc_read_prefs_t*>(read_prefs.RawReadPrefs()));
}

const void* MongoCollection::GetReadConcern() const {
    return impl_ && impl_->coll ? mongoc_collection_get_read_concern(impl_->coll) : nullptr;
}

void MongoCollection::SetReadConcern(const MongoReadConcern& read_concern) {
    if (impl_ && impl_->coll)
        mongoc_collection_set_read_concern(impl_->coll,
            static_cast<const mongoc_read_concern_t*>(read_concern.RawReadConcern()));
}

const void* MongoCollection::GetWriteConcern() const {
    return impl_ && impl_->coll ? mongoc_collection_get_write_concern(impl_->coll) : nullptr;
}

void MongoCollection::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->coll)
        mongoc_collection_set_write_concern(impl_->coll,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

bool MongoCollection::DropWithOpts(const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_drop_with_opts(impl_->coll,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::DropIndexWithOpts(const char* index_name, const BsonDocument* opts,
                                         MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_drop_index_with_opts(impl_->coll, index_name,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoCollection::FindIndexes(const BsonDocument* opts) {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_cursor_t* cursor = mongoc_collection_find_indexes_with_opts(impl_->coll,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

char* MongoCollection::KeysToIndexString(const BsonDocument& keys) const {
    if (!impl_ || !impl_->coll) return nullptr;
    return mongoc_collection_keys_to_index_string(
        static_cast<const bson_t*>(keys.RawBson()));
}

MongoCursor* MongoCollection::Aggregate(const BsonDocument& pipeline, const BsonDocument* opts,
                                         const MongoReadPrefs* read_prefs) {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_cursor_t* cursor = mongoc_collection_aggregate(impl_->coll,
        static_cast<mongoc_query_flags_t>(0),
        static_cast<const bson_t*>(pipeline.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

bool MongoCollection::InsertMany(const BsonDocument* documents[], size_t count,
                                  const BsonDocument* opts, BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    std::vector<const bson_t*> docs(count);
    for (size_t i = 0; i < count; ++i) {
        if (!documents[i]) return false;
        docs[i] = static_cast<const bson_t*>(documents[i]->RawBson());
    }
    return mongoc_collection_insert_many(impl_->coll, docs.data(), count,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::FindAndModify(const BsonDocument& query, const MongoFindAndModifyOpts* opts,
                                     BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_find_and_modify_with_opts(impl_->coll,
        static_cast<const bson_t*>(query.RawBson()),
        opts ? static_cast<const mongoc_find_and_modify_opts_t*>(opts->RawOpts()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoChangeStream* MongoCollection::Watch(const BsonDocument& pipeline, const BsonDocument* opts) {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_change_stream_t* stream = mongoc_collection_watch(impl_->coll,
        static_cast<const bson_t*>(pipeline.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!stream) return nullptr;
    auto* result = new MongoChangeStream();
    result->SetRawStream(stream);
    return result;
}

bool MongoCollection::DropIndex(const char* index_name, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_drop_index_with_opts(impl_->coll, index_name, nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::Rename(const char* new_db, const char* new_name,
                              bool drop_target_before_rename, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_rename_with_opts(impl_->coll, new_db, new_name,
        drop_target_before_rename, nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoCollection::RenameWithOpts(const char* new_db, const char* new_name,
                                      bool drop_target_before_rename,
                                      const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_rename_with_opts(impl_->coll, new_db, new_name,
        drop_target_before_rename,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

int64_t MongoCollection::EstimatedDocumentCount(const BsonDocument* opts,
                                                  const MongoReadPrefs* read_prefs,
                                                  MongoError* error) {
    if (!impl_ || !impl_->coll) return -1;
    return mongoc_collection_estimated_document_count(impl_->coll,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        read_prefs ? static_cast<const mongoc_read_prefs_t*>(read_prefs->RawReadPrefs()) : nullptr,
        nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoBulkOperation* MongoCollection::CreateBulkOperation(bool ordered, const void* session_raw) {
    if (!impl_ || !impl_->coll) return nullptr;
    BsonDocument opts;
    if (!ordered) opts.AppendBool("ordered", false);
    mongoc_bulk_operation_t* bulk = mongoc_collection_create_bulk_operation_with_opts(
        impl_->coll, static_cast<const bson_t*>(opts.RawBson()));
    if (!bulk) return nullptr;
    auto* result = new MongoBulkOperation();
    result->SetRawBulkOperation(bulk);
    if (session_raw)
        result->SetClientSession(const_cast<void*>(session_raw));
    return result;
}

MongoBulkOperation* MongoCollection::CreateBulkOperationWithOpts(const BsonDocument* opts) {
    if (!impl_ || !impl_->coll) return nullptr;
    mongoc_bulk_operation_t* bulk = mongoc_collection_create_bulk_operation_with_opts(
        impl_->coll,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!bulk) return nullptr;
    auto* result = new MongoBulkOperation();
    result->SetRawBulkOperation(bulk);
    return result;
}

bool MongoCollection::CreateIndexesWithOpts(const void* const* models, size_t n_models,
                                              const BsonDocument* opts,
                                              BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->coll) return false;
    return mongoc_collection_create_indexes_with_opts(impl_->coll,
        (mongoc_index_model_t* const*)(models), n_models,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

} // namespace mongo
} // namespace engine

#endif
