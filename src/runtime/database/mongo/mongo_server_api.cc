#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_server_api.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

struct MongoServerApi::Impl {
    mongoc_server_api_t* api = nullptr;
};

MongoServerApi MongoServerApi::New(Version version) {
    MongoServerApi result;
    result.impl_->api = mongoc_server_api_new(
        static_cast<mongoc_server_api_version_t>(version));
    return result;
}

const char* MongoServerApi::VersionToString(Version version) {
    return mongoc_server_api_version_to_string(
        static_cast<mongoc_server_api_version_t>(version));
}

bool MongoServerApi::VersionFromString(const char* str, Version* out) {
    if (!out) return false;
    mongoc_server_api_version_t raw;
    bool ok = mongoc_server_api_version_from_string(str, &raw);
    if (ok) *out = static_cast<Version>(raw);
    return ok;
}

MongoServerApi::MongoServerApi() : impl_(std::make_unique<Impl>()) {}

MongoServerApi::~MongoServerApi() {
    if (impl_ && impl_->api) mongoc_server_api_destroy(impl_->api);
}

MongoServerApi::MongoServerApi(MongoServerApi&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoServerApi& MongoServerApi::operator=(MongoServerApi&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->api) mongoc_server_api_destroy(impl_->api);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

MongoServerApi MongoServerApi::Copy() const {
    MongoServerApi result;
    if (impl_ && impl_->api) {
        result.impl_->api = mongoc_server_api_copy(impl_->api);
    }
    return result;
}

void MongoServerApi::SetStrict(bool strict) {
    if (impl_ && impl_->api) mongoc_server_api_strict(impl_->api, strict);
}

void MongoServerApi::SetDeprecationErrors(bool deprecation_errors) {
    if (impl_ && impl_->api) mongoc_server_api_deprecation_errors(impl_->api, deprecation_errors);
}

bool MongoServerApi::GetStrict() const {
    if (!impl_ || !impl_->api) return false;
    const mongoc_optional_t* opt = mongoc_server_api_get_strict(impl_->api);
    return mongoc_optional_is_set(opt) && mongoc_optional_value(opt);
}

bool MongoServerApi::GetDeprecationErrors() const {
    if (!impl_ || !impl_->api) return false;
    const mongoc_optional_t* opt = mongoc_server_api_get_deprecation_errors(impl_->api);
    return mongoc_optional_is_set(opt) && mongoc_optional_value(opt);
}

MongoServerApi::Version MongoServerApi::GetVersion() const {
    if (!impl_ || !impl_->api) return kV1;
    return static_cast<Version>(mongoc_server_api_get_version(impl_->api));
}

void* MongoServerApi::RawServerApi() {
    return impl_ ? impl_->api : nullptr;
}

const void* MongoServerApi::RawServerApi() const {
    return impl_ ? impl_->api : nullptr;
}

} // namespace mongo
} // namespace engine

#endif
