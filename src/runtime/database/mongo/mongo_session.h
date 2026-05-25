#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_transaction_opt_t for configuring transaction options.
class ENGINE_API MongoTransactionOpts {
public:
    MongoTransactionOpts();
    ~MongoTransactionOpts();

    MongoTransactionOpts(const MongoTransactionOpts&) = delete;
    MongoTransactionOpts& operator=(const MongoTransactionOpts&) = delete;
    MongoTransactionOpts(MongoTransactionOpts&& other) noexcept;
    MongoTransactionOpts& operator=(MongoTransactionOpts&& other) noexcept;

    MongoTransactionOpts Clone() const;

    void SetMaxCommitTimeMs(int64_t max_commit_time_ms);
    int64_t GetMaxCommitTimeMs() const;
    void SetReadConcern(const MongoReadConcern& read_concern);
    const void* GetReadConcernRaw() const;  // returns mongoc_read_concern_t*
    void SetWriteConcern(const MongoWriteConcern& write_concern);
    const void* GetWriteConcernRaw() const; // returns mongoc_write_concern_t*
    void SetReadPrefs(const MongoReadPrefs& read_prefs);
    const void* GetReadPrefsRaw() const;    // returns mongoc_read_prefs_t*

    void* RawTransactionOpts();
    const void* RawTransactionOpts() const; // returns mongoc_transaction_opt_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Wraps mongoc_session_opt_t for configuring session options.
class ENGINE_API MongoSessionOpts {
public:
    MongoSessionOpts();
    ~MongoSessionOpts();

    MongoSessionOpts(const MongoSessionOpts&) = delete;
    MongoSessionOpts& operator=(const MongoSessionOpts&) = delete;
    MongoSessionOpts(MongoSessionOpts&& other) noexcept;
    MongoSessionOpts& operator=(MongoSessionOpts&& other) noexcept;

    MongoSessionOpts Clone() const;

    void SetCausalConsistency(bool causal_consistency);
    bool GetCausalConsistency() const;
    void SetSnapshot(bool snapshot);
    bool GetSnapshot() const;
    void SetDefaultTransactionOpts(const MongoTransactionOpts& txn_opts);
    const void* GetDefaultTransactionOptsRaw() const; // returns mongoc_transaction_opt_t*

    void* RawSessionOpts();
    const void* RawSessionOpts() const; // returns mongoc_session_opt_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Wraps mongoc_client_session_t for causal consistency and transactions.
//
// Usage:
//   auto* session = client->StartSession(nullptr, &error);
//   session->StartTransaction(nullptr, &error);
//   // ... do work ...
//   session->CommitTransaction(nullptr, &error);
//   delete session;
class ENGINE_API MongoSession {
public:
    enum TransactionState {
        kNone = 0,
        kStarting = 1,
        kInProgress = 2,
        kCommitted = 3,
        kAborted = 4,
    };

    void Destroy();

    // ── Transaction ─────────────────────────────────────────────────
    bool StartTransaction(const MongoTransactionOpts* opts, MongoError* error);
    bool CommitTransaction(BsonDocument* reply, MongoError* error);
    bool AbortTransaction(MongoError* error);
    bool InTransaction() const;
    TransactionState GetTransactionState() const;

    // ── Cluster time / operation time ───────────────────────────────
    const void* GetClusterTimeRaw() const;  // returns const bson_t*
    void AdvanceClusterTime(const BsonDocument& cluster_time);
    void GetOperationTime(uint32_t* timestamp, uint32_t* increment) const;
    void AdvanceOperationTime(uint32_t timestamp, uint32_t increment);

    // ── Session info ────────────────────────────────────────────────
    const void* GetSessionIdRaw() const;    // returns const bson_t*
    uint32_t GetServerId() const;
    bool GetDirty() const;

    // ── With-transaction callback ─────────────────────────────────────
    // Callback receives (session, reply, error) — returns true on success.
    // reply is pre-allocated by the driver; user fills it with commit result.
    using WithTransactionCb = std::function<bool(MongoSession* session, BsonDocument* reply, MongoError* error)>;
    bool WithTransaction(const MongoTransactionOpts* opts,
                         WithTransactionCb cb, BsonDocument* reply, MongoError* error);

    // ── Transaction opts from current session ────────────────────────
    const void* GetTransactionOptsRaw() const; // returns mongoc_transaction_opt_t*

    // Append this session to an opts BSON document (for passing to CRUD ops).
    bool AppendToOpts(BsonDocument* opts, MongoError* error);

    void* GetClient() const;    // returns mongoc_client_t*
    const void* GetOpts() const; // returns mongoc_session_opt_t*

    void* RawSession(); // returns mongoc_client_session_t*
    void SetRawSession(void* session); // takes ownership, internal use
    void* ReleaseSession(); // releases ownership, returns raw session

    // Create an empty session wrapper (for internal trampoline use).
    static MongoSession* CreateEmpty();
    static void Destroy(MongoSession* session);

private:
    friend class MongoClient;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoSession();
    ~MongoSession();
    MongoSession(const MongoSession&) = delete;
    MongoSession& operator=(const MongoSession&) = delete;
    MongoSession(MongoSession&&) = delete;
    MongoSession& operator=(MongoSession&&) = delete;
};

} // namespace mongo
} // namespace engine
