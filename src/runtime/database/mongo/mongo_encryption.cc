#include "runtime/database/mongo/mongo_encryption.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// KMS callback trampoline
// ═══════════════════════════════════════════════════════════════════════

namespace {

struct KmsCallbackCtx {
    MongoKmsCredentialsProviderCb cb;
    void* userdata = nullptr;
};

bool kms_cred_provider_trampoline(void* userdata, const bson_t* params, bson_t* out, bson_error_t* error) {
    auto* ctx = static_cast<KmsCallbackCtx*>(userdata);
    if (!ctx || !ctx->cb) return false;
    BsonDocument params_doc;
    BsonDocument out_doc;
    MongoError mongo_err;
    bool ok = ctx->cb(ctx->userdata, params_doc, &out_doc, &mongo_err);
    if (ok && out) bson_copy_to(static_cast<const bson_t*>(out_doc.RawBson()), out);
    if (!ok && error) {
        auto* raw_err = static_cast<bson_error_t*>(mongo_err.RawError());
        memcpy(error, raw_err, sizeof(bson_error_t));
    }
    return ok;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// MongoAutoEncryptionOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoAutoEncryptionOpts::Impl {
    mongoc_auto_encryption_opts_t* opts = nullptr;
    std::unique_ptr<KmsCallbackCtx> kms_ctx;
};

MongoAutoEncryptionOpts::MongoAutoEncryptionOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_auto_encryption_opts_new();
}

MongoAutoEncryptionOpts::~MongoAutoEncryptionOpts() {
    if (impl_ && impl_->opts) mongoc_auto_encryption_opts_destroy(impl_->opts);
}

MongoAutoEncryptionOpts::MongoAutoEncryptionOpts(MongoAutoEncryptionOpts&&) noexcept = default;
MongoAutoEncryptionOpts& MongoAutoEncryptionOpts::operator=(MongoAutoEncryptionOpts&&) noexcept = default;

void MongoAutoEncryptionOpts::SetKeyvaultClient(void* client) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_keyvault_client(impl_->opts,
            static_cast<mongoc_client_t*>(client));
}

void MongoAutoEncryptionOpts::SetKeyvaultClientPool(void* pool) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_keyvault_client_pool(impl_->opts,
            static_cast<mongoc_client_pool_t*>(pool));
}

void MongoAutoEncryptionOpts::SetKeyvaultNamespace(const char* db, const char* coll) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_keyvault_namespace(impl_->opts, db, coll);
}

void MongoAutoEncryptionOpts::SetKmsProviders(const BsonDocument& kms_providers) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_kms_providers(impl_->opts,
            static_cast<const bson_t*>(kms_providers.RawBson()));
}

void MongoAutoEncryptionOpts::SetKeyExpiration(uint64_t expiration_ms) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_key_expiration(impl_->opts, expiration_ms);
}

void MongoAutoEncryptionOpts::SetTlsOpts(const BsonDocument& tls_opts) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_tls_opts(impl_->opts,
            static_cast<const bson_t*>(tls_opts.RawBson()));
}

void MongoAutoEncryptionOpts::SetSchemaMap(const BsonDocument& schema_map) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_schema_map(impl_->opts,
            static_cast<const bson_t*>(schema_map.RawBson()));
}

void MongoAutoEncryptionOpts::SetEncryptedFieldsMap(const BsonDocument& ef_map) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_encrypted_fields_map(impl_->opts,
            static_cast<const bson_t*>(ef_map.RawBson()));
}

void MongoAutoEncryptionOpts::SetBypassAutoEncryption(bool bypass) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_bypass_auto_encryption(impl_->opts, bypass);
}

void MongoAutoEncryptionOpts::SetBypassQueryAnalysis(bool bypass) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_bypass_query_analysis(impl_->opts, bypass);
}

void MongoAutoEncryptionOpts::SetExtra(const BsonDocument& extra) {
    if (impl_ && impl_->opts)
        mongoc_auto_encryption_opts_set_extra(impl_->opts,
            static_cast<const bson_t*>(extra.RawBson()));
}

void MongoAutoEncryptionOpts::SetKmsCredentialProviderCallback(MongoKmsCredentialsProviderCb cb,
                                                                void* userdata) {
    if (impl_ && impl_->opts) {
        impl_->kms_ctx = std::make_unique<KmsCallbackCtx>();
        impl_->kms_ctx->cb = std::move(cb);
        impl_->kms_ctx->userdata = userdata;
        mongoc_auto_encryption_opts_set_kms_credential_provider_callback(
            impl_->opts, kms_cred_provider_trampoline, impl_->kms_ctx.get());
    }
}

void* MongoAutoEncryptionOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoClientEncryptionOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoClientEncryptionOpts::Impl {
    mongoc_client_encryption_opts_t* opts = nullptr;
    std::unique_ptr<KmsCallbackCtx> kms_ctx;
};

MongoClientEncryptionOpts::MongoClientEncryptionOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_client_encryption_opts_new();
}

MongoClientEncryptionOpts::~MongoClientEncryptionOpts() {
    if (impl_ && impl_->opts) mongoc_client_encryption_opts_destroy(impl_->opts);
}

MongoClientEncryptionOpts::MongoClientEncryptionOpts(MongoClientEncryptionOpts&&) noexcept = default;
MongoClientEncryptionOpts& MongoClientEncryptionOpts::operator=(MongoClientEncryptionOpts&&) noexcept = default;

void MongoClientEncryptionOpts::SetKeyvaultClient(void* keyvault_client) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_opts_set_keyvault_client(impl_->opts,
            static_cast<mongoc_client_t*>(keyvault_client));
}

void MongoClientEncryptionOpts::SetKeyvaultNamespace(const char* db, const char* coll) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_opts_set_keyvault_namespace(impl_->opts, db, coll);
}

void MongoClientEncryptionOpts::SetKmsProviders(const BsonDocument& kms_providers) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_opts_set_kms_providers(impl_->opts,
            static_cast<const bson_t*>(kms_providers.RawBson()));
}

void MongoClientEncryptionOpts::SetTlsOpts(const BsonDocument& tls_opts) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_opts_set_tls_opts(impl_->opts,
            static_cast<const bson_t*>(tls_opts.RawBson()));
}

void MongoClientEncryptionOpts::SetKmsCredentialProviderCallback(MongoKmsCredentialsProviderCb cb,
                                                                   void* userdata) {
    if (impl_ && impl_->opts) {
        impl_->kms_ctx = std::make_unique<KmsCallbackCtx>();
        impl_->kms_ctx->cb = std::move(cb);
        impl_->kms_ctx->userdata = userdata;
        mongoc_client_encryption_opts_set_kms_credential_provider_callback(
            impl_->opts, kms_cred_provider_trampoline, impl_->kms_ctx.get());
    }
}

void MongoClientEncryptionOpts::SetKeyExpiration(uint64_t cache_expiration_ms) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_opts_set_key_expiration(impl_->opts, cache_expiration_ms);
}

void* MongoClientEncryptionOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoClientEncryptionEncryptOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoClientEncryptionEncryptOpts::Impl {
    mongoc_client_encryption_encrypt_opts_t* opts = nullptr;
};

MongoClientEncryptionEncryptOpts::MongoClientEncryptionEncryptOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_client_encryption_encrypt_opts_new();
}

MongoClientEncryptionEncryptOpts::~MongoClientEncryptionEncryptOpts() {
    if (impl_ && impl_->opts) mongoc_client_encryption_encrypt_opts_destroy(impl_->opts);
}

MongoClientEncryptionEncryptOpts::MongoClientEncryptionEncryptOpts(MongoClientEncryptionEncryptOpts&&) noexcept = default;
MongoClientEncryptionEncryptOpts& MongoClientEncryptionEncryptOpts::operator=(MongoClientEncryptionEncryptOpts&&) noexcept = default;

void MongoClientEncryptionEncryptOpts::SetKeyId(const void* keyid) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_encrypt_opts_set_keyid(impl_->opts,
            static_cast<const bson_value_t*>(keyid));
}

void MongoClientEncryptionEncryptOpts::SetKeyAltName(const char* keyaltname) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_encrypt_opts_set_keyaltname(impl_->opts, keyaltname);
}

void MongoClientEncryptionEncryptOpts::SetAlgorithm(const char* algorithm) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_encrypt_opts_set_algorithm(impl_->opts, algorithm);
}

void MongoClientEncryptionEncryptOpts::SetContentionFactor(int64_t factor) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_encrypt_opts_set_contention_factor(impl_->opts, factor);
}

void MongoClientEncryptionEncryptOpts::SetQueryType(const char* query_type) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_encrypt_opts_set_query_type(impl_->opts, query_type);
}

void* MongoClientEncryptionEncryptOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoClientEncryptionDatakeyOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoClientEncryptionDatakeyOpts::Impl {
    mongoc_client_encryption_datakey_opts_t* opts = nullptr;
};

MongoClientEncryptionDatakeyOpts::MongoClientEncryptionDatakeyOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_client_encryption_datakey_opts_new();
}

MongoClientEncryptionDatakeyOpts::~MongoClientEncryptionDatakeyOpts() {
    if (impl_ && impl_->opts) mongoc_client_encryption_datakey_opts_destroy(impl_->opts);
}

MongoClientEncryptionDatakeyOpts::MongoClientEncryptionDatakeyOpts(MongoClientEncryptionDatakeyOpts&&) noexcept = default;
MongoClientEncryptionDatakeyOpts& MongoClientEncryptionDatakeyOpts::operator=(MongoClientEncryptionDatakeyOpts&&) noexcept = default;

void MongoClientEncryptionDatakeyOpts::SetMasterkey(const BsonDocument& masterkey) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_datakey_opts_set_masterkey(impl_->opts,
            static_cast<const bson_t*>(masterkey.RawBson()));
}

void MongoClientEncryptionDatakeyOpts::SetKeyAltNames(char** keyaltnames, uint32_t count) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_datakey_opts_set_keyaltnames(impl_->opts, keyaltnames, count);
}

void MongoClientEncryptionDatakeyOpts::SetKeyMaterial(const uint8_t* data, uint32_t len) {
    if (impl_ && impl_->opts)
        mongoc_client_encryption_datakey_opts_set_keymaterial(impl_->opts, data, len);
}

void* MongoClientEncryptionDatakeyOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoClientEncryptionRewrapManyDatakeyResult
// ═══════════════════════════════════════════════════════════════════════

struct MongoClientEncryptionRewrapManyDatakeyResult::Impl {
    mongoc_client_encryption_rewrap_many_datakey_result_t* result = nullptr;
};

MongoClientEncryptionRewrapManyDatakeyResult::MongoClientEncryptionRewrapManyDatakeyResult()
    : impl_(std::make_unique<Impl>()) {
    impl_->result = mongoc_client_encryption_rewrap_many_datakey_result_new();
}

MongoClientEncryptionRewrapManyDatakeyResult::~MongoClientEncryptionRewrapManyDatakeyResult() {
    if (impl_ && impl_->result)
        mongoc_client_encryption_rewrap_many_datakey_result_destroy(impl_->result);
}

MongoClientEncryptionRewrapManyDatakeyResult::MongoClientEncryptionRewrapManyDatakeyResult(
    MongoClientEncryptionRewrapManyDatakeyResult&&) noexcept = default;
MongoClientEncryptionRewrapManyDatakeyResult& MongoClientEncryptionRewrapManyDatakeyResult::operator=(
    MongoClientEncryptionRewrapManyDatakeyResult&&) noexcept = default;

const void* MongoClientEncryptionRewrapManyDatakeyResult::GetBulkWriteResult() const {
    return impl_ && impl_->result
        ? mongoc_client_encryption_rewrap_many_datakey_result_get_bulk_write_result(impl_->result)
        : nullptr;
}

void* MongoClientEncryptionRewrapManyDatakeyResult::Raw() {
    return impl_ ? impl_->result : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoClientEncryption
// ═══════════════════════════════════════════════════════════════════════

struct MongoClientEncryption::Impl {
    mongoc_client_encryption_t* enc = nullptr;
};

MongoClientEncryption* MongoClientEncryption::New(MongoClientEncryptionOpts* opts, MongoError* error) {
    auto* e = new MongoClientEncryption();
    e->impl_->enc = mongoc_client_encryption_new(
        opts ? static_cast<mongoc_client_encryption_opts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!e->impl_->enc) {
        delete e;
        return nullptr;
    }
    return e;
}

MongoClientEncryption::MongoClientEncryption() : impl_(std::make_unique<Impl>()) {}
MongoClientEncryption::~MongoClientEncryption() { Destroy(); }

void MongoClientEncryption::Destroy() {
    if (impl_ && impl_->enc) {
        mongoc_client_encryption_destroy(impl_->enc);
        impl_->enc = nullptr;
    }
}

bool MongoClientEncryption::CreateDatakey(const char* kms_provider,
                                            const MongoClientEncryptionDatakeyOpts* opts,
                                            void* keyid_out, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_create_datakey(impl_->enc,
        kms_provider,
        opts ? static_cast<const mongoc_client_encryption_datakey_opts_t*>(opts->Raw()) : nullptr,
        static_cast<bson_value_t*>(keyid_out),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::RewrapManyDatakey(const BsonDocument& filter,
                                                const char* provider,
                                                const BsonDocument* master_key,
                                                MongoClientEncryptionRewrapManyDatakeyResult* result,
                                                MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_rewrap_many_datakey(impl_->enc,
        static_cast<const bson_t*>(filter.RawBson()), provider,
        master_key ? static_cast<const bson_t*>(master_key->RawBson()) : nullptr,
        result ? static_cast<mongoc_client_encryption_rewrap_many_datakey_result_t*>(result->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::DeleteKey(const void* keyid, BsonDocument* reply, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_delete_key(impl_->enc,
        static_cast<const bson_value_t*>(keyid),
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::GetKey(const void* keyid, BsonDocument* key_doc, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_get_key(impl_->enc,
        static_cast<const bson_value_t*>(keyid),
        key_doc ? static_cast<bson_t*>(key_doc->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoClientEncryption::GetKeys(MongoError* error) {
    if (!impl_ || !impl_->enc) return nullptr;
    mongoc_cursor_t* cursor = mongoc_client_encryption_get_keys(impl_->enc,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

bool MongoClientEncryption::AddKeyAltName(const void* keyid, const char* keyaltname,
                                             BsonDocument* key_doc, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_add_key_alt_name(impl_->enc,
        static_cast<const bson_value_t*>(keyid), keyaltname,
        key_doc ? static_cast<bson_t*>(key_doc->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::RemoveKeyAltName(const void* keyid, const char* keyaltname,
                                                BsonDocument* key_doc, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_remove_key_alt_name(impl_->enc,
        static_cast<const bson_value_t*>(keyid), keyaltname,
        key_doc ? static_cast<bson_t*>(key_doc->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::GetKeyByAltName(const char* keyaltname,
                                              BsonDocument* key_doc, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_get_key_by_alt_name(impl_->enc,
        keyaltname,
        key_doc ? static_cast<bson_t*>(key_doc->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::Encrypt(const void* value,
                                     MongoClientEncryptionEncryptOpts* opts,
                                     void* ciphertext_out, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_encrypt(impl_->enc,
        static_cast<const bson_value_t*>(value),
        opts ? static_cast<mongoc_client_encryption_encrypt_opts_t*>(opts->Raw()) : nullptr,
        static_cast<bson_value_t*>(ciphertext_out),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::EncryptExpression(const BsonDocument& expr,
                                                MongoClientEncryptionEncryptOpts* opts,
                                                BsonDocument* expr_out, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_encrypt_expression(impl_->enc,
        static_cast<const bson_t*>(expr.RawBson()),
        opts ? static_cast<mongoc_client_encryption_encrypt_opts_t*>(opts->Raw()) : nullptr,
        expr_out ? static_cast<bson_t*>(expr_out->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientEncryption::Decrypt(const void* ciphertext,
                                     void* value_out, MongoError* error) {
    return impl_ && impl_->enc && mongoc_client_encryption_decrypt(impl_->enc,
        static_cast<const bson_value_t*>(ciphertext),
        static_cast<bson_value_t*>(value_out),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoClientEncryption::CreateEncryptedCollection(void* database, const char* name,
                                                         const BsonDocument* in_options,
                                                         BsonDocument* out_options,
                                                         const char* kms_provider,
                                                         const BsonDocument* opt_masterkey,
                                                         MongoError* error) {
    if (!impl_ || !impl_->enc) return nullptr;
    return mongoc_client_encryption_create_encrypted_collection(impl_->enc,
        static_cast<mongoc_database_t*>(database), name,
        in_options ? static_cast<const bson_t*>(in_options->RawBson()) : nullptr,
        out_options ? static_cast<bson_t*>(out_options->RawBson()) : nullptr,
        kms_provider,
        opt_masterkey ? static_cast<const bson_t*>(opt_masterkey->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

const char* MongoClientEncryption::GetCryptSharedVersion() const {
    return impl_ && impl_->enc
        ? mongoc_client_encryption_get_crypt_shared_version(impl_->enc) : nullptr;
}

void* MongoClientEncryption::Raw() { return impl_ ? impl_->enc : nullptr; }

} // namespace mongo
} // namespace engine
