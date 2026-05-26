#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_find_and_modify_opts.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"

namespace engine {
namespace mongo {

struct MongoFindAndModifyOpts::Impl {
    mongoc_find_and_modify_opts_t* opts = nullptr;
};

MongoFindAndModifyOpts::MongoFindAndModifyOpts() : impl_(std::make_unique<Impl>()) {
    impl_->opts = mongoc_find_and_modify_opts_new();
}

MongoFindAndModifyOpts::~MongoFindAndModifyOpts() {
    if (impl_ && impl_->opts) mongoc_find_and_modify_opts_destroy(impl_->opts);
}

MongoFindAndModifyOpts::MongoFindAndModifyOpts(MongoFindAndModifyOpts&& other) noexcept
    : impl_(std::move(other.impl_)) {}

MongoFindAndModifyOpts& MongoFindAndModifyOpts::operator=(MongoFindAndModifyOpts&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->opts) mongoc_find_and_modify_opts_destroy(impl_->opts);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool MongoFindAndModifyOpts::SetSort(const BsonDocument& sort) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_sort(
        impl_->opts, static_cast<const bson_t*>(sort.RawBson()));
}

void MongoFindAndModifyOpts::GetSort(BsonDocument& out) const {
    if (impl_ && impl_->opts)
        mongoc_find_and_modify_opts_get_sort(
            impl_->opts, static_cast<bson_t*>(out.RawBson()));
}

bool MongoFindAndModifyOpts::SetUpdate(const BsonDocument& update) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_update(
        impl_->opts, static_cast<const bson_t*>(update.RawBson()));
}

void MongoFindAndModifyOpts::GetUpdate(BsonDocument& out) const {
    if (impl_ && impl_->opts)
        mongoc_find_and_modify_opts_get_update(
            impl_->opts, static_cast<bson_t*>(out.RawBson()));
}

bool MongoFindAndModifyOpts::SetFields(const BsonDocument& fields) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_fields(
        impl_->opts, static_cast<const bson_t*>(fields.RawBson()));
}

void MongoFindAndModifyOpts::GetFields(BsonDocument& out) const {
    if (impl_ && impl_->opts)
        mongoc_find_and_modify_opts_get_fields(
            impl_->opts, static_cast<bson_t*>(out.RawBson()));
}

bool MongoFindAndModifyOpts::SetFlags(uint32_t flags) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_flags(
        impl_->opts, static_cast<mongoc_find_and_modify_flags_t>(flags));
}

uint32_t MongoFindAndModifyOpts::GetFlags() const {
    if (!impl_ || !impl_->opts) return 0;
    return static_cast<uint32_t>(mongoc_find_and_modify_opts_get_flags(impl_->opts));
}

bool MongoFindAndModifyOpts::SetBypassDocumentValidation(bool bypass) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_bypass_document_validation(impl_->opts, bypass);
}

bool MongoFindAndModifyOpts::GetBypassDocumentValidation() const {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_get_bypass_document_validation(impl_->opts);
}

bool MongoFindAndModifyOpts::SetMaxTimeMs(uint32_t max_time_ms) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_set_max_time_ms(impl_->opts, max_time_ms);
}

uint32_t MongoFindAndModifyOpts::GetMaxTimeMs() const {
    return impl_ && impl_->opts ? mongoc_find_and_modify_opts_get_max_time_ms(impl_->opts) : 0;
}

bool MongoFindAndModifyOpts::Append(const BsonDocument& extra) {
    return impl_ && impl_->opts && mongoc_find_and_modify_opts_append(
        impl_->opts, static_cast<const bson_t*>(extra.RawBson()));
}

void MongoFindAndModifyOpts::GetExtra(BsonDocument& out) const {
    if (impl_ && impl_->opts)
        mongoc_find_and_modify_opts_get_extra(
            impl_->opts, static_cast<bson_t*>(out.RawBson()));
}

void* MongoFindAndModifyOpts::RawOpts() {
    return impl_ ? impl_->opts : nullptr;
}

const void* MongoFindAndModifyOpts::RawOpts() const {
    return impl_ ? impl_->opts : nullptr;
}

} // namespace mongo
} // namespace engine

#endif
