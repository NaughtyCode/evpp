#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class MongoHostList;
class MongoReadPrefs;

// Wraps mongoc-server-description.h — describes a single MongoDB server.
class ENGINE_API MongoServerDescription {
public:
    explicit MongoServerDescription(void* raw); // takes mongoc_server_description_t* (non-owning)
    ~MongoServerDescription() = default;

    MongoServerDescription(const MongoServerDescription&) = delete;
    MongoServerDescription& operator=(const MongoServerDescription&) = delete;

    uint32_t Id() const;
    const MongoHostList* Host() const;
    int64_t LastUpdateTime() const;
    int64_t RoundTripTime() const;
    const char* Type() const;
    const void* HelloResponse() const; // returns const bson_t*
    int32_t CompressorId() const;

    void DestroyCopy(); // call after NewCopy
    static MongoServerDescription* NewCopy(const MongoServerDescription* other);

    void* Raw() const;

private:
    void* sd_; // mongoc_server_description_t* (non-owning for originals, owning for copies)
    bool owns_ = false;
    mutable std::unique_ptr<MongoHostList> host_wrapper_;
};

// Wraps mongoc-topology-description.h — describes the entire cluster topology.
class ENGINE_API MongoTopologyDescription {
public:
    explicit MongoTopologyDescription(void* raw); // takes mongoc_topology_description_t* (non-owning)
    ~MongoTopologyDescription() = default;

    MongoTopologyDescription(const MongoTopologyDescription&) = delete;
    MongoTopologyDescription& operator=(const MongoTopologyDescription&) = delete;

    bool HasReadableServer(const MongoReadPrefs* prefs) const;
    bool HasWritableServer() const;
    const char* Type() const;

    // Returns newly allocated array of server descriptions. Caller must free each
    // with MongoServerDescription::DestroyCopy() and the array itself with free().
    MongoServerDescription** GetServers(size_t* n) const;

    void DestroyCopy(); // call after NewCopy
    static MongoTopologyDescription* NewCopy(const MongoTopologyDescription* other);

    void* Raw() const;

private:
    void* td_; // mongoc_topology_description_t*
    bool owns_ = false;
};

} // namespace mongo
} // namespace engine
