#include "runtime/database/mongo/mongo_session.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoTransactionOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoTransactionOpts::Impl {
    mongoc_transaction_opt_t* opts = nullptr;
};

MongoTransactionOpts::MongoTransactionOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_transaction_opts_new();
}

MongoTransactionOpts::~MongoTransactionOpts() {
    if (impl_ && impl_->opts) mongoc_transaction_opts_destroy(impl_->opts);
}

MongoTransactionOpts::MongoTransactionOpts(MongoTransactionOpts&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoTransactionOpts& MongoTransactionOpts::operator=(MongoTransactionOpts&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->opts) mongoc_transaction_opts_destroy(impl_->opts);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

MongoTransactionOpts MongoTransactionOpts::Clone() const {
    MongoTransactionOpts result;
    if (impl_ && impl_->opts) {
        mongoc_transaction_opts_destroy(result.impl_->opts);
        result.impl_->opts = mongoc_transaction_opts_clone(impl_->opts);
    }
    return result;
}

void MongoTransactionOpts::SetMaxCommitTimeMs(int64_t max_commit_time_ms) {
    if (impl_ && impl_->opts) mongoc_transaction_opts_set_max_commit_time_ms(impl_->opts, max_commit_time_ms);
}

int64_t MongoTransactionOpts::GetMaxCommitTimeMs() const {
    return impl_ && impl_->opts ? mongoc_transaction_opts_get_max_commit_time_ms(impl_->opts) : 0;
}

void MongoTransactionOpts::SetReadConcern(const MongoReadConcern& read_concern) {
    if (impl_ && impl_->opts)
        mongoc_transaction_opts_set_read_concern(impl_->opts,
            static_cast<const mongoc_read_concern_t*>(read_concern.RawReadConcern()));
}

const void* MongoTransactionOpts::GetReadConcernRaw() const {
    return impl_ && impl_->opts ? mongoc_transaction_opts_get_read_concern(impl_->opts) : nullptr;
}

void MongoTransactionOpts::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->opts)
        mongoc_transaction_opts_set_write_concern(impl_->opts,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

const void* MongoTransactionOpts::GetWriteConcernRaw() const {
    return impl_ && impl_->opts ? mongoc_transaction_opts_get_write_concern(impl_->opts) : nullptr;
}

void MongoTransactionOpts::SetReadPrefs(const MongoReadPrefs& read_prefs) {
    if (impl_ && impl_->opts)
        mongoc_transaction_opts_set_read_prefs(impl_->opts,
            static_cast<const mongoc_read_prefs_t*>(read_prefs.RawReadPrefs()));
}

const void* MongoTransactionOpts::GetReadPrefsRaw() const {
    return impl_ && impl_->opts ? mongoc_transaction_opts_get_read_prefs(impl_->opts) : nullptr;
}

void* MongoTransactionOpts::RawTransactionOpts() {
    return impl_ ? impl_->opts : nullptr;
}

const void* MongoTransactionOpts::RawTransactionOpts() const {
    return impl_ ? impl_->opts : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoSessionOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoSessionOpts::Impl {
    mongoc_session_opt_t* opts = nullptr;
};

MongoSessionOpts::MongoSessionOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_session_opts_new();
}

MongoSessionOpts::~MongoSessionOpts() {
    if (impl_ && impl_->opts) mongoc_session_opts_destroy(impl_->opts);
}

MongoSessionOpts::MongoSessionOpts(MongoSessionOpts&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoSessionOpts& MongoSessionOpts::operator=(MongoSessionOpts&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->opts) mongoc_session_opts_destroy(impl_->opts);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

MongoSessionOpts MongoSessionOpts::Clone() const {
    MongoSessionOpts result;
    if (impl_ && impl_->opts) {
        mongoc_session_opts_destroy(result.impl_->opts);
        result.impl_->opts = mongoc_session_opts_clone(impl_->opts);
    }
    return result;
}

void MongoSessionOpts::SetCausalConsistency(bool causal_consistency) {
    if (impl_ && impl_->opts) mongoc_session_opts_set_causal_consistency(impl_->opts, causal_consistency);
}

bool MongoSessionOpts::GetCausalConsistency() const {
    return impl_ && impl_->opts && mongoc_session_opts_get_causal_consistency(impl_->opts);
}

void MongoSessionOpts::SetSnapshot(bool snapshot) {
    if (impl_ && impl_->opts) mongoc_session_opts_set_snapshot(impl_->opts, snapshot);
}

bool MongoSessionOpts::GetSnapshot() const {
    return impl_ && impl_->opts && mongoc_session_opts_get_snapshot(impl_->opts);
}

void MongoSessionOpts::SetDefaultTransactionOpts(const MongoTransactionOpts& txn_opts) {
    if (impl_ && impl_->opts)
        mongoc_session_opts_set_default_transaction_opts(impl_->opts,
            static_cast<const mongoc_transaction_opt_t*>(txn_opts.RawTransactionOpts()));
}

const void* MongoSessionOpts::GetDefaultTransactionOptsRaw() const {
    return impl_ && impl_->opts
        ? mongoc_session_opts_get_default_transaction_opts(impl_->opts) : nullptr;
}

void* MongoSessionOpts::RawSessionOpts() {
    return impl_ ? impl_->opts : nullptr;
}

const void* MongoSessionOpts::RawSessionOpts() const {
    return impl_ ? impl_->opts : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoSession
// ═══════════════════════════════════════════════════════════════════════

struct MongoSession::Impl {
    mongoc_client_session_t* session = nullptr;
};

MongoSession::MongoSession() : impl_(std::make_unique<Impl>()) {}
MongoSession::~MongoSession() { Destroy(); }

void MongoSession::Destroy() {
    if (impl_ && impl_->session) {
        mongoc_client_session_destroy(impl_->session);
        impl_->session = nullptr;
    }
}

bool MongoSession::StartTransaction(const MongoTransactionOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->session) return false;
    return mongoc_client_session_start_transaction(impl_->session,
        opts ? static_cast<const mongoc_transaction_opt_t*>(opts->RawTransactionOpts()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoSession::CommitTransaction(BsonDocument* reply, MongoError* error) {
    if (!impl_ || !impl_->session) return false;
    return mongoc_client_session_commit_transaction(impl_->session,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoSession::AbortTransaction(MongoError* error) {
    if (!impl_ || !impl_->session) return false;
    return mongoc_client_session_abort_transaction(impl_->session,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoSession::InTransaction() const {
    return impl_ && impl_->session && mongoc_client_session_in_transaction(impl_->session);
}

MongoSession::TransactionState MongoSession::GetTransactionState() const {
    if (!impl_ || !impl_->session) return kNone;
    return static_cast<TransactionState>(mongoc_client_session_get_transaction_state(impl_->session));
}

const void* MongoSession::GetClusterTimeRaw() const {
    return impl_ && impl_->session ? mongoc_client_session_get_cluster_time(impl_->session) : nullptr;
}

void MongoSession::AdvanceClusterTime(const BsonDocument& cluster_time) {
    if (impl_ && impl_->session)
        mongoc_client_session_advance_cluster_time(impl_->session,
            static_cast<const bson_t*>(cluster_time.RawBson()));
}

void MongoSession::GetOperationTime(uint32_t* timestamp, uint32_t* increment) const {
    if (impl_ && impl_->session)
        mongoc_client_session_get_operation_time(impl_->session, timestamp, increment);
}

void MongoSession::AdvanceOperationTime(uint32_t timestamp, uint32_t increment) {
    if (impl_ && impl_->session)
        mongoc_client_session_advance_operation_time(impl_->session, timestamp, increment);
}

const void* MongoSession::GetSessionIdRaw() const {
    return impl_ && impl_->session ? mongoc_client_session_get_lsid(impl_->session) : nullptr;
}

uint32_t MongoSession::GetServerId() const {
    return impl_ && impl_->session ? mongoc_client_session_get_server_id(impl_->session) : 0;
}

bool MongoSession::AppendToOpts(BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->session) return false;
    return mongoc_client_session_append(impl_->session,
        static_cast<bson_t*>(opts->RawBson()),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoSession::GetClient() const {
    return impl_ && impl_->session ? mongoc_client_session_get_client(impl_->session) : nullptr;
}

const void* MongoSession::GetOpts() const {
    return impl_ && impl_->session ? mongoc_client_session_get_opts(impl_->session) : nullptr;
}

void* MongoSession::RawSession() {
    return impl_ ? impl_->session : nullptr;
}

bool MongoSession::GetDirty() const {
    return impl_ && impl_->session && mongoc_client_session_get_dirty(impl_->session);
}

const void* MongoSession::GetTransactionOptsRaw() const {
    return impl_ && impl_->session
        ? mongoc_session_opts_get_transaction_opts(impl_->session) : nullptr;
}

MongoSession* MongoSession::CreateEmpty() {
    return new MongoSession();
}

void MongoSession::Destroy(MongoSession* session) {
    delete session;
}

namespace {

struct WithTxnCtx {
    MongoSession::WithTransactionCb cb;
};

bool with_transaction_trampoline(mongoc_client_session_t* session,
                                   void* ctx, bson_t** reply, bson_error_t* error) {
    auto* txn_ctx = static_cast<WithTxnCtx*>(ctx);
    if (!txn_ctx || !txn_ctx->cb) return false;

    MongoSession* tmp_session = MongoSession::CreateEmpty();
    tmp_session->SetRawSession(session);

    BsonDocument reply_doc;
    MongoError mongo_err;

    bool ok = false;
    try {
        ok = txn_ctx->cb(tmp_session, &reply_doc, &mongo_err);
    } catch (...) {
        tmp_session->ReleaseSession();
        MongoSession::Destroy(tmp_session);
        return false;
    }

    tmp_session->ReleaseSession();
    MongoSession::Destroy(tmp_session);

    if (reply && ok) {
        bson_destroy(*reply);
        *reply = bson_copy(static_cast<const bson_t*>(reply_doc.RawBson()));
    }
    if (!ok && error) {
        auto* raw_err = static_cast<bson_error_t*>(mongo_err.RawError());
        if (raw_err) memcpy(error, raw_err, sizeof(bson_error_t));
    }
    return ok;
}

} // namespace

bool MongoSession::WithTransaction(const MongoTransactionOpts* opts,
                                     WithTransactionCb cb, BsonDocument* reply,
                                     MongoError* error) {
    if (!impl_ || !impl_->session || !cb) return false;

    WithTxnCtx txn_ctx{std::move(cb)};

    return mongoc_client_session_with_transaction(
        impl_->session,
        with_transaction_trampoline,
        opts ? static_cast<const mongoc_transaction_opt_t*>(opts->RawTransactionOpts()) : nullptr,
        &txn_ctx,
        reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoSession::ReleaseSession() {
    if (!impl_) return nullptr;
    void* s = impl_->session;
    impl_->session = nullptr;
    return s;
}

void MongoSession::SetRawSession(void* session) {
    if (impl_ && impl_->session) mongoc_client_session_destroy(impl_->session);
    impl_->session = static_cast<mongoc_client_session_t*>(session);
}

} // namespace mongo
} // namespace engine
