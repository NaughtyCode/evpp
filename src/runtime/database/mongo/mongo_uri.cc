#include "runtime/database/mongo/mongo_uri.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

struct MongoUri::Impl {
    mongoc_uri_t* uri = nullptr;
};

MongoUri MongoUri::New(const char* uri_string) {
    MongoUri result;
    result.impl_ = std::make_unique<Impl>();
    result.impl_->uri = mongoc_uri_new(uri_string);
    return result;
}

MongoUri MongoUri::NewForHostPort(const char* hostname, uint16_t port) {
    MongoUri result;
    result.impl_ = std::make_unique<Impl>();
    result.impl_->uri = mongoc_uri_new_for_host_port(hostname, port);
    return result;
}

MongoUri::MongoUri() : impl_(std::make_unique<Impl>()) {}

MongoUri::~MongoUri() {
    if (impl_ && impl_->uri) {
        mongoc_uri_destroy(impl_->uri);
    }
}

MongoUri::MongoUri(MongoUri&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoUri& MongoUri::operator=(MongoUri&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

MongoUri MongoUri::Copy() const {
    MongoUri result;
    if (impl_ && impl_->uri) {
        result.impl_->uri = mongoc_uri_copy(impl_->uri);
    }
    return result;
}

const char* MongoUri::GetString() const {
    return impl_ && impl_->uri ? mongoc_uri_get_string(impl_->uri) : nullptr;
}

const char* MongoUri::GetDatabase() const {
    return impl_ && impl_->uri ? mongoc_uri_get_database(impl_->uri) : nullptr;
}

const char* MongoUri::GetUsername() const {
    return impl_ && impl_->uri ? mongoc_uri_get_username(impl_->uri) : nullptr;
}

const char* MongoUri::GetPassword() const {
    return impl_ && impl_->uri ? mongoc_uri_get_password(impl_->uri) : nullptr;
}

const char* MongoUri::GetAuthSource() const {
    return impl_ && impl_->uri ? mongoc_uri_get_auth_source(impl_->uri) : nullptr;
}

const char* MongoUri::GetAuthMechanism() const {
    return impl_ && impl_->uri ? mongoc_uri_get_auth_mechanism(impl_->uri) : nullptr;
}

const char* MongoUri::GetReplicaSet() const {
    return impl_ && impl_->uri ? mongoc_uri_get_replica_set(impl_->uri) : nullptr;
}

const char* MongoUri::GetAppname() const {
    return impl_ && impl_->uri ? mongoc_uri_get_appname(impl_->uri) : nullptr;
}

bool MongoUri::SetDatabase(const char* database) {
    return impl_ && impl_->uri && mongoc_uri_set_database(impl_->uri, database);
}

bool MongoUri::SetUsername(const char* username) {
    return impl_ && impl_->uri && mongoc_uri_set_username(impl_->uri, username);
}

bool MongoUri::SetPassword(const char* password) {
    return impl_ && impl_->uri && mongoc_uri_set_password(impl_->uri, password);
}

bool MongoUri::SetAuthSource(const char* value) {
    return impl_ && impl_->uri && mongoc_uri_set_auth_source(impl_->uri, value);
}

bool MongoUri::SetAuthMechanism(const char* value) {
    return impl_ && impl_->uri && mongoc_uri_set_auth_mechanism(impl_->uri, value);
}

bool MongoUri::SetAppname(const char* appname) {
    return impl_ && impl_->uri && mongoc_uri_set_appname(impl_->uri, appname);
}

void* MongoUri::RawUri() {
    return impl_ ? impl_->uri : nullptr;
}

const void* MongoUri::RawUri() const {
    return impl_ ? impl_->uri : nullptr;
}

void MongoUri::SetRawUri(void* uri) {
    if (impl_ && impl_->uri) mongoc_uri_destroy(impl_->uri);
    impl_->uri = static_cast<mongoc_uri_t*>(uri);
}

// ── New URI accessors ────────────────────────────────────────────────

const char* MongoUri::GetSrvHostname() const {
    return impl_ && impl_->uri ? mongoc_uri_get_srv_hostname(impl_->uri) : nullptr;
}

const char* MongoUri::GetSrvServiceName() const {
    return impl_ && impl_->uri ? mongoc_uri_get_srv_service_name(impl_->uri) : nullptr;
}

const void* MongoUri::GetCompressors() const {
    return impl_ && impl_->uri ? mongoc_uri_get_compressors(impl_->uri) : nullptr;
}

const void* MongoUri::GetCredentials() const {
    return impl_ && impl_->uri ? mongoc_uri_get_credentials(impl_->uri) : nullptr;
}

bool MongoUri::GetTls() const {
    return impl_ && impl_->uri && mongoc_uri_get_tls(impl_->uri);
}

bool MongoUri::HasOption(const char* key) const {
    return impl_ && impl_->uri && mongoc_uri_has_option(impl_->uri, key);
}

bool MongoUri::SetCompressors(const char* compressors) {
    return impl_ && impl_->uri && mongoc_uri_set_compressors(impl_->uri, compressors);
}

void MongoUri::SetReadPrefs(const MongoReadPrefs& read_prefs) {
    if (impl_ && impl_->uri)
        mongoc_uri_set_read_prefs_t(impl_->uri,
            static_cast<const mongoc_read_prefs_t*>(read_prefs.RawReadPrefs()));
}

void MongoUri::SetWriteConcern(const MongoWriteConcern& write_concern) {
    if (impl_ && impl_->uri)
        mongoc_uri_set_write_concern(impl_->uri,
            static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

void MongoUri::SetReadConcern(const MongoReadConcern& read_concern) {
    if (impl_ && impl_->uri)
        mongoc_uri_set_read_concern(impl_->uri,
            static_cast<const mongoc_read_concern_t*>(read_concern.RawReadConcern()));
}

} // namespace mongo
} // namespace engine
