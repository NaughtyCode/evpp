#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_uri_t for parsing and constructing MongoDB connection URIs.
//
// Usage:
//   auto uri = MongoUri::New("mongodb://localhost:27017");
//   const char* db = uri.GetDatabase();
//   uri.SetAppname("MyApp");
class ENGINE_API MongoUri {
public:
    static MongoUri New(const char* uri_string);
    static MongoUri NewForHostPort(const char* hostname, uint16_t port);

    MongoUri();
    ~MongoUri();

    MongoUri(const MongoUri&) = delete;
    MongoUri& operator=(const MongoUri&) = delete;
    MongoUri(MongoUri&& other) noexcept;
    MongoUri& operator=(MongoUri&& other) noexcept;

    MongoUri Copy() const;

    // Accessors
    const char* GetString() const;
    const char* GetDatabase() const;
    const char* GetUsername() const;
    const char* GetPassword() const;
    const char* GetAuthSource() const;
    const char* GetAuthMechanism() const;
    const char* GetReplicaSet() const;
    const char* GetAppname() const;

    // Setters
    bool SetDatabase(const char* database);
    bool SetUsername(const char* username);
    bool SetPassword(const char* password);
    bool SetAuthSource(const char* value);
    bool SetAuthMechanism(const char* value);
    bool SetAppname(const char* appname);

    // Internal access
    void* RawUri();              // returns mongoc_uri_t*
    const void* RawUri() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
