#include "runtime/database/mongo/mongo_topology.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_host_list.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoServerDescription
// ═══════════════════════════════════════════════════════════════════════

MongoServerDescription::MongoServerDescription(void* raw) : sd_(raw) {}

uint32_t MongoServerDescription::Id() const {
    return sd_ ? mongoc_server_description_id(static_cast<mongoc_server_description_t*>(sd_)) : 0;
}

const MongoHostList* MongoServerDescription::Host() const {
    if (!sd_) return nullptr;
    if (!host_wrapper_) {
        const auto* raw_host = mongoc_server_description_host(
            static_cast<const mongoc_server_description_t*>(sd_));
        if (!raw_host) return nullptr;
        auto* wrapper = new MongoHostList();
        // Copy host data into the wrapper
        auto* dst = static_cast<mongoc_host_list_t*>(wrapper->Raw());
        memcpy(dst, raw_host, sizeof(mongoc_host_list_t));
        dst->next = nullptr; // don't follow the list
        host_wrapper_.reset(wrapper);
    }
    return host_wrapper_.get();
}

int64_t MongoServerDescription::LastUpdateTime() const {
    return sd_ ? mongoc_server_description_last_update_time(
        static_cast<const mongoc_server_description_t*>(sd_)) : 0;
}

int64_t MongoServerDescription::RoundTripTime() const {
    return sd_ ? mongoc_server_description_round_trip_time(
        static_cast<const mongoc_server_description_t*>(sd_)) : 0;
}

const char* MongoServerDescription::Type() const {
    return sd_ ? mongoc_server_description_type(
        static_cast<const mongoc_server_description_t*>(sd_)) : nullptr;
}

const void* MongoServerDescription::HelloResponse() const {
    return sd_ ? mongoc_server_description_hello_response(
        static_cast<const mongoc_server_description_t*>(sd_)) : nullptr;
}

int32_t MongoServerDescription::CompressorId() const {
    return sd_ ? mongoc_server_description_compressor_id(
        static_cast<const mongoc_server_description_t*>(sd_)) : 0;
}

MongoServerDescription* MongoServerDescription::NewCopy(const MongoServerDescription* other) {
    if (!other || !other->sd_) return nullptr;
    auto* raw_copy = mongoc_server_description_new_copy(
        static_cast<const mongoc_server_description_t*>(other->sd_));
    if (!raw_copy) return nullptr;
    auto* result = new MongoServerDescription(raw_copy);
    result->owns_ = true;
    return result;
}

void MongoServerDescription::DestroyCopy() {
    if (owns_ && sd_) {
        mongoc_server_description_destroy(static_cast<mongoc_server_description_t*>(sd_));
        sd_ = nullptr;
        owns_ = false;
    }
}

void* MongoServerDescription::Raw() const { return sd_; }

// ═══════════════════════════════════════════════════════════════════════
// MongoTopologyDescription
// ═══════════════════════════════════════════════════════════════════════

MongoTopologyDescription::MongoTopologyDescription(void* raw) : td_(raw) {}

bool MongoTopologyDescription::HasReadableServer(const MongoReadPrefs* prefs) const {
    if (!td_) return false;
    return mongoc_topology_description_has_readable_server(
        static_cast<const mongoc_topology_description_t*>(td_),
        prefs ? static_cast<const mongoc_read_prefs_t*>(prefs->RawReadPrefs()) : nullptr);
}

bool MongoTopologyDescription::HasWritableServer() const {
    return td_ && mongoc_topology_description_has_writable_server(
        static_cast<const mongoc_topology_description_t*>(td_));
}

const char* MongoTopologyDescription::Type() const {
    return td_ ? mongoc_topology_description_type(
        static_cast<const mongoc_topology_description_t*>(td_)) : nullptr;
}

MongoServerDescription** MongoTopologyDescription::GetServers(size_t* n) const {
    if (!td_) return nullptr;
    size_t count = 0;
    auto** raw_servers = mongoc_topology_description_get_servers(
        static_cast<const mongoc_topology_description_t*>(td_), &count);
    if (!raw_servers || count == 0) {
        if (n) *n = 0;
        return nullptr;
    }
    auto** result = static_cast<MongoServerDescription**>(
        bson_malloc(count * sizeof(MongoServerDescription*)));
    for (size_t i = 0; i < count; ++i) {
        result[i] = new MongoServerDescription(raw_servers[i]);
    }
    bson_free(raw_servers);
    if (n) *n = count;
    return result;
}

MongoTopologyDescription* MongoTopologyDescription::NewCopy(const MongoTopologyDescription* other) {
    if (!other || !other->td_) return nullptr;
    auto* raw_copy = mongoc_topology_description_new_copy(
        static_cast<const mongoc_topology_description_t*>(other->td_));
    if (!raw_copy) return nullptr;
    auto* result = new MongoTopologyDescription(raw_copy);
    result->owns_ = true;
    return result;
}

void MongoTopologyDescription::DestroyCopy() {
    if (owns_ && td_) {
        mongoc_topology_description_destroy(static_cast<mongoc_topology_description_t*>(td_));
        td_ = nullptr;
        owns_ = false;
    }
}

void* MongoTopologyDescription::Raw() const { return td_; }

} // namespace mongo
} // namespace engine
