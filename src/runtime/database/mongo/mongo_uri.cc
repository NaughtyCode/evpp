#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_uri.h"
#include "runtime/database/mongo/mongo_bson.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

struct MongoUri::Impl {
    mongoc_uri_t* uri = nullptr;
};

MongoUri MongoUri::New(const char* uri_string) {
    MongoUri result;
    result.impl_->uri = mongoc_uri_new(uri_string);
    return result;
}

MongoUri MongoUri::NewWithError(const char* uri_string, MongoError* error) {
    MongoUri result;
    result.impl_->uri = mongoc_uri_new_with_error(uri_string,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    return result;
}

MongoUri MongoUri::NewForHostPort(const char* hostname, uint16_t port) {
    MongoUri result;
    result.impl_->uri = mongoc_uri_new_for_host_port(hostname, port);
    return result;
}

char* MongoUri::Unescape(const char* escaped_string) {
    return mongoc_uri_unescape(escaped_string);
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
        if (impl_ && impl_->uri) mongoc_uri_destroy(impl_->uri);
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
    if (!impl_) return;
    if (impl_->uri) mongoc_uri_destroy(impl_->uri);
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

// ── Hosts / Options / Mechanism ─────────────────────────────────────

const void* MongoUri::GetHosts() const {
    return impl_ && impl_->uri ? mongoc_uri_get_hosts(impl_->uri) : nullptr;
}

const void* MongoUri::GetOptions() const {
    return impl_ && impl_->uri ? mongoc_uri_get_options(impl_->uri) : nullptr;
}

bool MongoUri::GetMechanismProperties(BsonDocument& properties) const {
    if (!impl_ || !impl_->uri) return false;
    return mongoc_uri_get_mechanism_properties(impl_->uri,
        static_cast<bson_t*>(properties.RawBson()));
}

bool MongoUri::SetMechanismProperties(const BsonDocument& properties) {
    if (!impl_ || !impl_->uri) return false;
    return mongoc_uri_set_mechanism_properties(impl_->uri,
        static_cast<const bson_t*>(properties.RawBson()));
}

// ── Settings getters ──────────────────────────────────────────────────

const void* MongoUri::GetReadPrefs() const {
    return impl_ && impl_->uri ? mongoc_uri_get_read_prefs_t(impl_->uri) : nullptr;
}

const void* MongoUri::GetWriteConcern() const {
    return impl_ && impl_->uri ? mongoc_uri_get_write_concern(impl_->uri) : nullptr;
}

const void* MongoUri::GetReadConcern() const {
    return impl_ && impl_->uri ? mongoc_uri_get_read_concern(impl_->uri) : nullptr;
}

// ── Server monitoring mode ───────────────────────────────────────────

const char* MongoUri::GetServerMonitoringMode() const {
    return impl_ && impl_->uri ? mongoc_uri_get_server_monitoring_mode(impl_->uri) : nullptr;
}

bool MongoUri::SetServerMonitoringMode(const char* value) {
    return impl_ && impl_->uri && mongoc_uri_set_server_monitoring_mode(impl_->uri, value);
}

// ── Option type checks (static) ──────────────────────────────────────

bool MongoUri::OptionIsInt32(const char* key) { return mongoc_uri_option_is_int32(key); }
bool MongoUri::OptionIsInt64(const char* key) { return mongoc_uri_option_is_int64(key); }
bool MongoUri::OptionIsBool(const char* key)  { return mongoc_uri_option_is_bool(key); }
bool MongoUri::OptionIsUtf8(const char* key)  { return mongoc_uri_option_is_utf8(key); }

// ── Generic option getters ────────────────────────────────────────────

int32_t MongoUri::GetOptionAsInt32(const char* option, int32_t fallback) const {
    return impl_ && impl_->uri
        ? mongoc_uri_get_option_as_int32(impl_->uri, option, fallback) : fallback;
}

int64_t MongoUri::GetOptionAsInt64(const char* option, int64_t fallback) const {
    return impl_ && impl_->uri
        ? mongoc_uri_get_option_as_int64(impl_->uri, option, fallback) : fallback;
}

bool MongoUri::GetOptionAsBool(const char* option, bool fallback) const {
    return impl_ && impl_->uri
        ? mongoc_uri_get_option_as_bool(impl_->uri, option, fallback) : fallback;
}

const char* MongoUri::GetOptionAsUtf8(const char* option, const char* fallback) const {
    return impl_ && impl_->uri
        ? mongoc_uri_get_option_as_utf8(impl_->uri, option, fallback) : fallback;
}

// ── Generic option setters ────────────────────────────────────────────

bool MongoUri::SetOptionAsInt32(const char* option, int32_t value) {
    return impl_ && impl_->uri && mongoc_uri_set_option_as_int32(impl_->uri, option, value);
}

bool MongoUri::SetOptionAsInt64(const char* option, int64_t value) {
    return impl_ && impl_->uri && mongoc_uri_set_option_as_int64(impl_->uri, option, value);
}

bool MongoUri::SetOptionAsBool(const char* option, bool value) {
    return impl_ && impl_->uri && mongoc_uri_set_option_as_bool(impl_->uri, option, value);
}

bool MongoUri::SetOptionAsUtf8(const char* option, const char* value) {
    return impl_ && impl_->uri && mongoc_uri_set_option_as_utf8(impl_->uri, option, value);
}

} // namespace mongo
} // namespace engine

#endif
