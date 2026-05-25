#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo/mongo_bson.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoReadPrefs
// ═══════════════════════════════════════════════════════════════════════

struct MongoReadPrefs::Impl {
    mongoc_read_prefs_t* prefs = nullptr;
};

MongoReadPrefs::MongoReadPrefs() : impl_(std::make_unique<Impl>()) {
    impl_->prefs = mongoc_read_prefs_new(MONGOC_READ_PRIMARY);
}

MongoReadPrefs::MongoReadPrefs(Mode mode) : impl_(std::make_unique<Impl>()) {
    impl_->prefs = mongoc_read_prefs_new(static_cast<mongoc_read_mode_t>(mode));
}

MongoReadPrefs::~MongoReadPrefs() {
    if (impl_ && impl_->prefs) {
        mongoc_read_prefs_destroy(impl_->prefs);
    }
}

MongoReadPrefs::MongoReadPrefs(MongoReadPrefs&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoReadPrefs& MongoReadPrefs::operator=(MongoReadPrefs&& other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

MongoReadPrefs MongoReadPrefs::Copy() const {
    MongoReadPrefs result;
    if (impl_ && impl_->prefs) {
        mongoc_read_prefs_destroy(result.impl_->prefs);
        result.impl_->prefs = mongoc_read_prefs_copy(impl_->prefs);
    }
    return result;
}

MongoReadPrefs::Mode MongoReadPrefs::GetMode() const {
    if (!impl_ || !impl_->prefs) return kPrimary;
    return static_cast<Mode>(mongoc_read_prefs_get_mode(impl_->prefs));
}

void MongoReadPrefs::SetMode(Mode mode) {
    if (impl_ && impl_->prefs) {
        mongoc_read_prefs_set_mode(impl_->prefs, static_cast<mongoc_read_mode_t>(mode));
    }
}

void* MongoReadPrefs::RawReadPrefs() {
    return impl_ ? impl_->prefs : nullptr;
}

const void* MongoReadPrefs::RawReadPrefs() const {
    return impl_ ? impl_->prefs : nullptr;
}

const void* MongoReadPrefs::GetTags() const {
    return impl_ && impl_->prefs ? mongoc_read_prefs_get_tags(impl_->prefs) : nullptr;
}

void MongoReadPrefs::SetTags(const BsonDocument& tags) {
    if (impl_ && impl_->prefs)
        mongoc_read_prefs_set_tags(impl_->prefs,
            static_cast<const bson_t*>(tags.RawBson()));
}

bool MongoReadPrefs::AddTag(const BsonDocument& tag) {
    if (!impl_ || !impl_->prefs) return false;
    mongoc_read_prefs_add_tag(impl_->prefs,
        static_cast<const bson_t*>(tag.RawBson()));
    return true;
}

int MongoReadPrefs::GetMaxStalenessSeconds() const {
    return impl_ && impl_->prefs
        ? static_cast<int>(mongoc_read_prefs_get_max_staleness_seconds(impl_->prefs)) : 0;
}

void MongoReadPrefs::SetMaxStalenessSeconds(int max_staleness_seconds) {
    if (impl_ && impl_->prefs)
        mongoc_read_prefs_set_max_staleness_seconds(impl_->prefs, static_cast<int64_t>(max_staleness_seconds));
}

const void* MongoReadPrefs::GetHedge() const {
    return impl_ && impl_->prefs ? mongoc_read_prefs_get_hedge(impl_->prefs) : nullptr;
}

void MongoReadPrefs::SetHedge(const BsonDocument& hedge) {
    if (impl_ && impl_->prefs)
        mongoc_read_prefs_set_hedge(impl_->prefs,
            static_cast<const bson_t*>(hedge.RawBson()));
}

bool MongoReadPrefs::IsValid() const {
    return impl_ && impl_->prefs && mongoc_read_prefs_is_valid(impl_->prefs);
}

// ═══════════════════════════════════════════════════════════════════════
// MongoWriteConcern
// ═══════════════════════════════════════════════════════════════════════

struct MongoWriteConcern::Impl {
    mongoc_write_concern_t* wc = nullptr;
};

MongoWriteConcern::MongoWriteConcern() : impl_(std::make_unique<Impl>()) {
    impl_->wc = mongoc_write_concern_new();
}

MongoWriteConcern::~MongoWriteConcern() {
    if (impl_ && impl_->wc) {
        mongoc_write_concern_destroy(impl_->wc);
    }
}

MongoWriteConcern::MongoWriteConcern(MongoWriteConcern&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoWriteConcern& MongoWriteConcern::operator=(MongoWriteConcern&& other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

MongoWriteConcern MongoWriteConcern::Copy() const {
    MongoWriteConcern result;
    if (impl_ && impl_->wc) {
        mongoc_write_concern_destroy(result.impl_->wc);
        result.impl_->wc = mongoc_write_concern_copy(impl_->wc);
    }
    return result;
}

int32_t MongoWriteConcern::GetW() const {
    return impl_ && impl_->wc ? mongoc_write_concern_get_w(impl_->wc) : 0;
}

void MongoWriteConcern::SetW(int32_t w) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_w(impl_->wc, w);
}

bool MongoWriteConcern::GetJournal() const {
    return impl_ && impl_->wc && mongoc_write_concern_get_journal(impl_->wc);
}

void MongoWriteConcern::SetJournal(bool journal) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_journal(impl_->wc, journal);
}

int32_t MongoWriteConcern::GetWTimeout() const {
    return impl_ && impl_->wc ? mongoc_write_concern_get_wtimeout(impl_->wc) : 0;
}

void MongoWriteConcern::SetWTimeout(int32_t timeout_ms) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_wtimeout(impl_->wc, timeout_ms);
}

bool MongoWriteConcern::IsAcknowledged() const {
    return impl_ && impl_->wc && mongoc_write_concern_is_acknowledged(impl_->wc);
}

bool MongoWriteConcern::JournalIsSet() const {
    return impl_ && impl_->wc && mongoc_write_concern_journal_is_set(impl_->wc);
}

int64_t MongoWriteConcern::GetWTimeoutInt64() const {
    return impl_ && impl_->wc ? mongoc_write_concern_get_wtimeout_int64(impl_->wc) : 0;
}

void MongoWriteConcern::SetWTimeoutInt64(int64_t timeout_ms) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_wtimeout_int64(impl_->wc, timeout_ms);
}

bool MongoWriteConcern::GetWMajority() const {
    return impl_ && impl_->wc && mongoc_write_concern_get_wmajority(impl_->wc);
}

void MongoWriteConcern::SetWMajority(int32_t wtimeout_msec) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_wmajority(impl_->wc, wtimeout_msec);
}

const char* MongoWriteConcern::GetWTag() const {
    return impl_ && impl_->wc ? mongoc_write_concern_get_wtag(impl_->wc) : nullptr;
}

int32_t MongoWriteConcern::SetWTag(const char* tag) {
    if (impl_ && impl_->wc) mongoc_write_concern_set_wtag(impl_->wc, tag);
    return 0;
}

bool MongoWriteConcern::IsValid() const {
    return impl_ && impl_->wc && mongoc_write_concern_is_valid(impl_->wc);
}

bool MongoWriteConcern::IsDefault() const {
    return impl_ && impl_->wc && mongoc_write_concern_is_default(impl_->wc);
}

bool MongoWriteConcern::AppendToOpts(BsonDocument& opts) const {
    if (!impl_ || !impl_->wc) return false;
    auto* cmd = static_cast<bson_t*>(opts.RawBson());
    return mongoc_write_concern_append(impl_->wc, cmd);
}

void* MongoWriteConcern::RawWriteConcern() {
    return impl_ ? impl_->wc : nullptr;
}

const void* MongoWriteConcern::RawWriteConcern() const {
    return impl_ ? impl_->wc : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoReadConcern
// ═══════════════════════════════════════════════════════════════════════

struct MongoReadConcern::Impl {
    mongoc_read_concern_t* rc = nullptr;
};

MongoReadConcern::MongoReadConcern() : impl_(std::make_unique<Impl>()) {
    impl_->rc = mongoc_read_concern_new();
}

MongoReadConcern::~MongoReadConcern() {
    if (impl_ && impl_->rc) {
        mongoc_read_concern_destroy(impl_->rc);
    }
}

MongoReadConcern::MongoReadConcern(MongoReadConcern&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoReadConcern& MongoReadConcern::operator=(MongoReadConcern&& other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

MongoReadConcern MongoReadConcern::Copy() const {
    MongoReadConcern result;
    if (impl_ && impl_->rc) {
        mongoc_read_concern_destroy(result.impl_->rc);
        result.impl_->rc = mongoc_read_concern_copy(impl_->rc);
    }
    return result;
}

const char* MongoReadConcern::GetLevel() const {
    return impl_ && impl_->rc ? mongoc_read_concern_get_level(impl_->rc) : nullptr;
}

bool MongoReadConcern::SetLevel(const char* level) {
    return impl_ && impl_->rc && mongoc_read_concern_set_level(impl_->rc, level);
}

bool MongoReadConcern::IsDefault() const {
    return impl_ && impl_->rc && mongoc_read_concern_is_default(impl_->rc);
}

bool MongoReadConcern::AppendToOpts(BsonDocument& opts) const {
    if (!impl_ || !impl_->rc) return false;
    auto* cmd = static_cast<bson_t*>(opts.RawBson());
    return mongoc_read_concern_append(impl_->rc, cmd);
}

void* MongoReadConcern::RawReadConcern() {
    return impl_ ? impl_->rc : nullptr;
}

const void* MongoReadConcern::RawReadConcern() const {
    return impl_ ? impl_->rc : nullptr;
}

} // namespace mongo
} // namespace engine
