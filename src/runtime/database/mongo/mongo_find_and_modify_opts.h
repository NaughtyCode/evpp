#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Flags for find-and-modify operations.
enum MongoFindAndModifyFlags : uint32_t {
    kFindAndModifyNone     = 0,
    kFindAndModifyRemove   = 1 << 0,
    kFindAndModifyUpsert   = 1 << 1,
    kFindAndModifyReturnNew = 1 << 2,
};

class ENGINE_API MongoFindAndModifyOpts {
public:
    MongoFindAndModifyOpts();
    ~MongoFindAndModifyOpts();

    MongoFindAndModifyOpts(const MongoFindAndModifyOpts&) = delete;
    MongoFindAndModifyOpts& operator=(const MongoFindAndModifyOpts&) = delete;
    MongoFindAndModifyOpts(MongoFindAndModifyOpts&& other) noexcept;
    MongoFindAndModifyOpts& operator=(MongoFindAndModifyOpts&& other) noexcept;

    // ── Sort ─────────────────────────────────────────────────────────
    bool SetSort(const BsonDocument& sort);
    void GetSort(BsonDocument& out) const;

    // ── Update ───────────────────────────────────────────────────────
    bool SetUpdate(const BsonDocument& update);
    void GetUpdate(BsonDocument& out) const;

    // ── Fields (projection) ───────────────────────────────────────────
    bool SetFields(const BsonDocument& fields);
    void GetFields(BsonDocument& out) const;

    // ── Flags ─────────────────────────────────────────────────────────
    bool SetFlags(uint32_t flags);
    uint32_t GetFlags() const;

    // ── Bypass document validation ────────────────────────────────────
    bool SetBypassDocumentValidation(bool bypass);
    bool GetBypassDocumentValidation() const;

    // ── Max time ──────────────────────────────────────────────────────
    bool SetMaxTimeMs(uint32_t max_time_ms);
    uint32_t GetMaxTimeMs() const;

    // ── Extra options ─────────────────────────────────────────────────
    bool Append(const BsonDocument& extra);
    void GetExtra(BsonDocument& out) const;

    // ── Internal access ───────────────────────────────────────────────
    void* RawOpts();
    const void* RawOpts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
