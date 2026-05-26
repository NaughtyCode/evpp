#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoClientPool {
public:
    // Create a pool from a connection URI.
    static MongoClientPool* New(const MongoUri& uri);
    static MongoClientPool* New(const MongoUri& uri, MongoError* error);

    void Destroy();

    // ═══ Disallow copy/move ═══════════════════════════════════════
    MongoClientPool(const MongoClientPool&) = delete;
    MongoClientPool& operator=(const MongoClientPool&) = delete;
    MongoClientPool(MongoClientPool&&) = delete;
    MongoClientPool& operator=(MongoClientPool&&) = delete;

    // ── Pool operations ──────────────────────────────────────────────
    MongoClient* Pop();       // borrow a client; caller MUST Push() or destroy
    void Push(MongoClient* client);       // return client to pool
    MongoClient* TryPop();    // non-blocking pop; returns nullptr if none available
    void SetMaxSize(uint32_t max_pool_size);

    // ── Configuration ─────────────────────────────────────────────────
    void SetSslOpts(const void* ssl_opts);
    bool SetApmCallbacks(void* callbacks, void* context);
    bool SetErrorApi(uint32_t version);
    bool SetAppname(const char* appname);
    bool SetServerApi(const MongoServerApi& api, MongoError* error);
    bool AppendMetadata(const char* name, const char* version, const char* platform);

    // ── Auto-encryption ──────────────────────────────────────────────
    bool EnableAutoEncryption(void* opts, MongoError* error);

    // ── Structured logging ───────────────────────────────────────────
    bool SetStructuredLogOpts(const void* opts);

    // ── OIDC callback ────────────────────────────────────────────────
    bool SetOidcCallback(const void* callback);

    // Internal
    void* RawPool(); // returns mongoc_client_pool_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    MongoClientPool();

public:
    ~MongoClientPool();
};

} // namespace mongo
} // namespace engine

#endif
