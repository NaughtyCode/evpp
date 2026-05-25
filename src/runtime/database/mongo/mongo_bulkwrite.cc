#include "runtime/database/mongo/mongo_bulkwrite.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteInsertOneOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteInsertOneOpts::Impl {
    mongoc_bulkwrite_insertoneopts_t* opts = nullptr;
};

MongoBulkWriteInsertOneOpts::MongoBulkWriteInsertOneOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_insertoneopts_new();
}

MongoBulkWriteInsertOneOpts::~MongoBulkWriteInsertOneOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_insertoneopts_destroy(impl_->opts);
}

MongoBulkWriteInsertOneOpts::MongoBulkWriteInsertOneOpts(MongoBulkWriteInsertOneOpts&&) noexcept = default;
MongoBulkWriteInsertOneOpts& MongoBulkWriteInsertOneOpts::operator=(MongoBulkWriteInsertOneOpts&&) noexcept = default;

void* MongoBulkWriteInsertOneOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteUpdateOneOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteUpdateOneOpts::Impl {
    mongoc_bulkwrite_updateoneopts_t* opts = nullptr;
};

MongoBulkWriteUpdateOneOpts::MongoBulkWriteUpdateOneOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_updateoneopts_new();
}

MongoBulkWriteUpdateOneOpts::~MongoBulkWriteUpdateOneOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_updateoneopts_destroy(impl_->opts);
}

MongoBulkWriteUpdateOneOpts::MongoBulkWriteUpdateOneOpts(MongoBulkWriteUpdateOneOpts&&) noexcept = default;
MongoBulkWriteUpdateOneOpts& MongoBulkWriteUpdateOneOpts::operator=(MongoBulkWriteUpdateOneOpts&&) noexcept = default;

void MongoBulkWriteUpdateOneOpts::SetArrayFilters(const BsonDocument& array_filters) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updateoneopts_set_arrayfilters(impl_->opts,
            static_cast<const bson_t*>(array_filters.RawBson()));
}

void MongoBulkWriteUpdateOneOpts::SetCollation(const BsonDocument& collation) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updateoneopts_set_collation(impl_->opts,
            static_cast<const bson_t*>(collation.RawBson()));
}

void MongoBulkWriteUpdateOneOpts::SetHint(const void* hint) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updateoneopts_set_hint(impl_->opts,
            static_cast<const bson_value_t*>(hint));
}

void MongoBulkWriteUpdateOneOpts::SetUpsert(bool upsert) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updateoneopts_set_upsert(impl_->opts, upsert);
}

void MongoBulkWriteUpdateOneOpts::SetSort(const BsonDocument& sort) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updateoneopts_set_sort(impl_->opts,
            static_cast<const bson_t*>(sort.RawBson()));
}

void* MongoBulkWriteUpdateOneOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteUpdateManyOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteUpdateManyOpts::Impl {
    mongoc_bulkwrite_updatemanyopts_t* opts = nullptr;
};

MongoBulkWriteUpdateManyOpts::MongoBulkWriteUpdateManyOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_updatemanyopts_new();
}

MongoBulkWriteUpdateManyOpts::~MongoBulkWriteUpdateManyOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_updatemanyopts_destroy(impl_->opts);
}

MongoBulkWriteUpdateManyOpts::MongoBulkWriteUpdateManyOpts(MongoBulkWriteUpdateManyOpts&&) noexcept = default;
MongoBulkWriteUpdateManyOpts& MongoBulkWriteUpdateManyOpts::operator=(MongoBulkWriteUpdateManyOpts&&) noexcept = default;

void MongoBulkWriteUpdateManyOpts::SetArrayFilters(const BsonDocument& array_filters) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updatemanyopts_set_arrayfilters(impl_->opts,
            static_cast<const bson_t*>(array_filters.RawBson()));
}

void MongoBulkWriteUpdateManyOpts::SetCollation(const BsonDocument& collation) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updatemanyopts_set_collation(impl_->opts,
            static_cast<const bson_t*>(collation.RawBson()));
}

void MongoBulkWriteUpdateManyOpts::SetHint(const void* hint) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updatemanyopts_set_hint(impl_->opts,
            static_cast<const bson_value_t*>(hint));
}

void MongoBulkWriteUpdateManyOpts::SetUpsert(bool upsert) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_updatemanyopts_set_upsert(impl_->opts, upsert);
}

void* MongoBulkWriteUpdateManyOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteReplaceOneOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteReplaceOneOpts::Impl {
    mongoc_bulkwrite_replaceoneopts_t* opts = nullptr;
};

MongoBulkWriteReplaceOneOpts::MongoBulkWriteReplaceOneOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_replaceoneopts_new();
}

MongoBulkWriteReplaceOneOpts::~MongoBulkWriteReplaceOneOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_replaceoneopts_destroy(impl_->opts);
}

MongoBulkWriteReplaceOneOpts::MongoBulkWriteReplaceOneOpts(MongoBulkWriteReplaceOneOpts&&) noexcept = default;
MongoBulkWriteReplaceOneOpts& MongoBulkWriteReplaceOneOpts::operator=(MongoBulkWriteReplaceOneOpts&&) noexcept = default;

void MongoBulkWriteReplaceOneOpts::SetCollation(const BsonDocument& collation) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_replaceoneopts_set_collation(impl_->opts,
            static_cast<const bson_t*>(collation.RawBson()));
}

void MongoBulkWriteReplaceOneOpts::SetHint(const void* hint) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_replaceoneopts_set_hint(impl_->opts,
            static_cast<const bson_value_t*>(hint));
}

void MongoBulkWriteReplaceOneOpts::SetUpsert(bool upsert) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_replaceoneopts_set_upsert(impl_->opts, upsert);
}

void MongoBulkWriteReplaceOneOpts::SetSort(const BsonDocument& sort) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_replaceoneopts_set_sort(impl_->opts,
            static_cast<const bson_t*>(sort.RawBson()));
}

void* MongoBulkWriteReplaceOneOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteDeleteOneOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteDeleteOneOpts::Impl {
    mongoc_bulkwrite_deleteoneopts_t* opts = nullptr;
};

MongoBulkWriteDeleteOneOpts::MongoBulkWriteDeleteOneOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_deleteoneopts_new();
}

MongoBulkWriteDeleteOneOpts::~MongoBulkWriteDeleteOneOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_deleteoneopts_destroy(impl_->opts);
}

MongoBulkWriteDeleteOneOpts::MongoBulkWriteDeleteOneOpts(MongoBulkWriteDeleteOneOpts&&) noexcept = default;
MongoBulkWriteDeleteOneOpts& MongoBulkWriteDeleteOneOpts::operator=(MongoBulkWriteDeleteOneOpts&&) noexcept = default;

void MongoBulkWriteDeleteOneOpts::SetCollation(const BsonDocument& collation) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_deleteoneopts_set_collation(impl_->opts,
            static_cast<const bson_t*>(collation.RawBson()));
}

void MongoBulkWriteDeleteOneOpts::SetHint(const void* hint) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_deleteoneopts_set_hint(impl_->opts,
            static_cast<const bson_value_t*>(hint));
}

void* MongoBulkWriteDeleteOneOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteDeleteManyOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteDeleteManyOpts::Impl {
    mongoc_bulkwrite_deletemanyopts_t* opts = nullptr;
};

MongoBulkWriteDeleteManyOpts::MongoBulkWriteDeleteManyOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwrite_deletemanyopts_new();
}

MongoBulkWriteDeleteManyOpts::~MongoBulkWriteDeleteManyOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwrite_deletemanyopts_destroy(impl_->opts);
}

MongoBulkWriteDeleteManyOpts::MongoBulkWriteDeleteManyOpts(MongoBulkWriteDeleteManyOpts&&) noexcept = default;
MongoBulkWriteDeleteManyOpts& MongoBulkWriteDeleteManyOpts::operator=(MongoBulkWriteDeleteManyOpts&&) noexcept = default;

void MongoBulkWriteDeleteManyOpts::SetCollation(const BsonDocument& collation) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_deletemanyopts_set_collation(impl_->opts,
            static_cast<const bson_t*>(collation.RawBson()));
}

void MongoBulkWriteDeleteManyOpts::SetHint(const void* hint) {
    if (impl_ && impl_->opts)
        mongoc_bulkwrite_deletemanyopts_set_hint(impl_->opts,
            static_cast<const bson_value_t*>(hint));
}

void* MongoBulkWriteDeleteManyOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteOpts::Impl {
    mongoc_bulkwriteopts_t* opts = nullptr;
};

MongoBulkWriteOpts::MongoBulkWriteOpts()
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_bulkwriteopts_new();
}

MongoBulkWriteOpts::~MongoBulkWriteOpts() {
    if (impl_ && impl_->opts) mongoc_bulkwriteopts_destroy(impl_->opts);
}

MongoBulkWriteOpts::MongoBulkWriteOpts(MongoBulkWriteOpts&&) noexcept = default;
MongoBulkWriteOpts& MongoBulkWriteOpts::operator=(MongoBulkWriteOpts&&) noexcept = default;

void MongoBulkWriteOpts::SetOrdered(bool ordered) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_ordered(impl_->opts, ordered);
}

void MongoBulkWriteOpts::SetBypassDocumentValidation(bool bypass) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_bypassdocumentvalidation(impl_->opts, bypass);
}

void MongoBulkWriteOpts::SetLet(const BsonDocument& let) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_let(impl_->opts,
            static_cast<const bson_t*>(let.RawBson()));
}

void MongoBulkWriteOpts::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_writeconcern(impl_->opts,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

void MongoBulkWriteOpts::SetComment(const void* comment) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_comment(impl_->opts,
            static_cast<const bson_value_t*>(comment));
}

void MongoBulkWriteOpts::SetVerboseResults(bool verbose) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_verboseresults(impl_->opts, verbose);
}

void MongoBulkWriteOpts::SetExtra(const BsonDocument& extra) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_extra(impl_->opts,
            static_cast<const bson_t*>(extra.RawBson()));
}

void MongoBulkWriteOpts::SetServerId(uint32_t server_id) {
    if (impl_ && impl_->opts)
        mongoc_bulkwriteopts_set_serverid(impl_->opts, server_id);
}

void* MongoBulkWriteOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteResult
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteResult::Impl {
    mongoc_bulkwriteresult_t* result = nullptr;
};

MongoBulkWriteResult::MongoBulkWriteResult() : impl_(std::make_unique<Impl>()) {}
MongoBulkWriteResult::~MongoBulkWriteResult() {
    if (impl_ && impl_->result) mongoc_bulkwriteresult_destroy(impl_->result);
}

MongoBulkWriteResult::MongoBulkWriteResult(MongoBulkWriteResult&&) noexcept = default;
MongoBulkWriteResult& MongoBulkWriteResult::operator=(MongoBulkWriteResult&&) noexcept = default;

int64_t MongoBulkWriteResult::InsertedCount() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_insertedcount(impl_->result) : 0;
}

int64_t MongoBulkWriteResult::UpsertedCount() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_upsertedcount(impl_->result) : 0;
}

int64_t MongoBulkWriteResult::MatchedCount() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_matchedcount(impl_->result) : 0;
}

int64_t MongoBulkWriteResult::ModifiedCount() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_modifiedcount(impl_->result) : 0;
}

int64_t MongoBulkWriteResult::DeletedCount() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_deletedcount(impl_->result) : 0;
}

const void* MongoBulkWriteResult::InsertResults() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_insertresults(impl_->result) : nullptr;
}

const void* MongoBulkWriteResult::UpdateResults() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_updateresults(impl_->result) : nullptr;
}

const void* MongoBulkWriteResult::DeleteResults() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_deleteresults(impl_->result) : nullptr;
}

uint32_t MongoBulkWriteResult::ServerId() const {
    return impl_ && impl_->result ? mongoc_bulkwriteresult_serverid(impl_->result) : 0;
}

void* MongoBulkWriteResult::Raw() { return impl_ ? impl_->result : nullptr; }

void MongoBulkWriteResult::SetRaw(void* raw) {
    if (impl_) impl_->result = static_cast<mongoc_bulkwriteresult_t*>(raw);
}

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWriteException
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWriteException::Impl {
    mongoc_bulkwriteexception_t* exc = nullptr;
};

MongoBulkWriteException::MongoBulkWriteException() : impl_(std::make_unique<Impl>()) {}
MongoBulkWriteException::~MongoBulkWriteException() {
    if (impl_ && impl_->exc) mongoc_bulkwriteexception_destroy(impl_->exc);
}

MongoBulkWriteException::MongoBulkWriteException(MongoBulkWriteException&&) noexcept = default;
MongoBulkWriteException& MongoBulkWriteException::operator=(MongoBulkWriteException&&) noexcept = default;

bool MongoBulkWriteException::Error(MongoError* error) const {
    return impl_ && impl_->exc && mongoc_bulkwriteexception_error(impl_->exc,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

const void* MongoBulkWriteException::WriteErrors() const {
    return impl_ && impl_->exc ? mongoc_bulkwriteexception_writeerrors(impl_->exc) : nullptr;
}

const void* MongoBulkWriteException::WriteConcernErrors() const {
    return impl_ && impl_->exc ? mongoc_bulkwriteexception_writeconcernerrors(impl_->exc) : nullptr;
}

const void* MongoBulkWriteException::ErrorReply() const {
    return impl_ && impl_->exc ? mongoc_bulkwriteexception_errorreply(impl_->exc) : nullptr;
}

void* MongoBulkWriteException::Raw() { return impl_ ? impl_->exc : nullptr; }

void MongoBulkWriteException::SetRaw(void* raw) {
    if (impl_) impl_->exc = static_cast<mongoc_bulkwriteexception_t*>(raw);
}

// ═══════════════════════════════════════════════════════════════════════
// MongoBulkWrite
// ═══════════════════════════════════════════════════════════════════════

struct MongoBulkWrite::Impl {
    mongoc_bulkwrite_t* bw = nullptr;
};

MongoBulkWrite* MongoBulkWrite::New(void* raw_client) {
    auto* b = new MongoBulkWrite();
    b->impl_->bw = mongoc_client_bulkwrite_new(static_cast<mongoc_client_t*>(raw_client));
    if (!b->impl_->bw) {
        delete b;
        return nullptr;
    }
    return b;
}

MongoBulkWrite* MongoBulkWrite::New() {
    auto* b = new MongoBulkWrite();
    b->impl_->bw = mongoc_bulkwrite_new();
    if (!b->impl_->bw) {
        delete b;
        return nullptr;
    }
    return b;
}

MongoBulkWrite::MongoBulkWrite() : impl_(std::make_unique<Impl>()) {}

MongoBulkWrite::~MongoBulkWrite() {
    Destroy();
}

void MongoBulkWrite::Destroy() {
    if (impl_ && impl_->bw) {
        mongoc_bulkwrite_destroy(impl_->bw);
        impl_->bw = nullptr;
    }
}

bool MongoBulkWrite::AppendInsertOne(const char* ns, const BsonDocument& document,
                                      const MongoBulkWriteInsertOneOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_insertone(impl_->bw, ns,
        static_cast<const bson_t*>(document.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_insertoneopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkWrite::AppendUpdateOne(const char* ns, const BsonDocument& filter,
                                      const BsonDocument& update,
                                      const MongoBulkWriteUpdateOneOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_updateone(impl_->bw, ns,
        static_cast<const bson_t*>(filter.RawBson()),
        static_cast<const bson_t*>(update.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_updateoneopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkWrite::AppendUpdateMany(const char* ns, const BsonDocument& filter,
                                       const BsonDocument& update,
                                       const MongoBulkWriteUpdateManyOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_updatemany(impl_->bw, ns,
        static_cast<const bson_t*>(filter.RawBson()),
        static_cast<const bson_t*>(update.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_updatemanyopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkWrite::AppendReplaceOne(const char* ns, const BsonDocument& filter,
                                       const BsonDocument& replacement,
                                       const MongoBulkWriteReplaceOneOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_replaceone(impl_->bw, ns,
        static_cast<const bson_t*>(filter.RawBson()),
        static_cast<const bson_t*>(replacement.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_replaceoneopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkWrite::AppendDeleteOne(const char* ns, const BsonDocument& filter,
                                      const MongoBulkWriteDeleteOneOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_deleteone(impl_->bw, ns,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_deleteoneopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkWrite::AppendDeleteMany(const char* ns, const BsonDocument& filter,
                                       const MongoBulkWriteDeleteManyOpts* opts, MongoError* error) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_append_deletemany(impl_->bw, ns,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const mongoc_bulkwrite_deletemanyopts_t*>(opts->Raw()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoBulkWriteReturn MongoBulkWrite::Execute(const MongoBulkWriteOpts* opts) {
    MongoBulkWriteReturn ret;
    if (!impl_ || !impl_->bw) return ret;

    mongoc_bulkwritereturn_t raw_ret = mongoc_bulkwrite_execute(impl_->bw,
        opts ? static_cast<const mongoc_bulkwriteopts_t*>(opts->Raw()) : nullptr);

    if (raw_ret.res) {
        auto* result = new MongoBulkWriteResult();
        result->SetRaw(raw_ret.res);
        ret.result = result;
    }
    if (raw_ret.exc) {
        auto* exc = new MongoBulkWriteException();
        exc->SetRaw(raw_ret.exc);
        ret.exception = exc;
    }
    return ret;
}

MongoBulkWriteCheckAcknowledged MongoBulkWrite::CheckAcknowledged(MongoError* error) const {
    MongoBulkWriteCheckAcknowledged ret;
    if (!impl_ || !impl_->bw) return ret;
    mongoc_bulkwrite_check_acknowledged_t raw =
        mongoc_bulkwrite_check_acknowledged(impl_->bw,
            error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    ret.is_ok = raw.is_ok;
    ret.is_acknowledged = raw.is_acknowledged;
    return ret;
}

MongoBulkWriteServerId MongoBulkWrite::ServerId(MongoError* error) const {
    MongoBulkWriteServerId ret;
    if (!impl_ || !impl_->bw) return ret;
    mongoc_bulkwrite_serverid_t raw =
        mongoc_bulkwrite_serverid(impl_->bw,
            error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    ret.is_ok = raw.is_ok;
    ret.server_id = raw.serverid;
    return ret;
}

void MongoBulkWrite::SetSession(void* session) {
    if (impl_ && impl_->bw)
        mongoc_bulkwrite_set_session(impl_->bw,
            static_cast<mongoc_client_session_t*>(session));
}

bool MongoBulkWrite::SetClient(void* client) {
    if (!impl_ || !impl_->bw) return false;
    return mongoc_bulkwrite_set_client(impl_->bw,
        static_cast<mongoc_client_t*>(client));
}

void* MongoBulkWrite::Raw() { return impl_ ? impl_->bw : nullptr; }

} // namespace mongo
} // namespace engine
