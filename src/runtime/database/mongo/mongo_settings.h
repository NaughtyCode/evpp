#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoReadPrefs {
public:
    enum Mode {
        kPrimary = 0,
        kSecondary = 1,
        kPrimaryPreferred = 2,
        kSecondaryPreferred = 3,
        kNearest = 4,
    };

    MongoReadPrefs();
    explicit MongoReadPrefs(Mode mode);
    ~MongoReadPrefs();

    MongoReadPrefs(const MongoReadPrefs&) = delete;
    MongoReadPrefs& operator=(const MongoReadPrefs&) = delete;
    MongoReadPrefs(MongoReadPrefs&& other) noexcept;
    MongoReadPrefs& operator=(MongoReadPrefs&& other) noexcept;

    MongoReadPrefs Copy() const;

    Mode GetMode() const;
    void SetMode(Mode mode);

    // Tag sets (used for targeting specific replicas)
    bool AddTag(const BsonDocument& tag);
    int GetMaxStalenessSeconds() const;
    void SetMaxStalenessSeconds(int max_staleness_seconds);
    bool IsValid() const;

    void* RawReadPrefs();       // returns mongoc_read_prefs_t*
    const void* RawReadPrefs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ENGINE_API MongoWriteConcern {
public:
    enum WValue {
        kUnacknowledged = 0,
        kDefault = -2,
        kMajority = -3,
        kTag = -4,
    };

    MongoWriteConcern();
    ~MongoWriteConcern();

    MongoWriteConcern(const MongoWriteConcern&) = delete;
    MongoWriteConcern& operator=(const MongoWriteConcern&) = delete;
    MongoWriteConcern(MongoWriteConcern&& other) noexcept;
    MongoWriteConcern& operator=(MongoWriteConcern&& other) noexcept;

    MongoWriteConcern Copy() const;

    int32_t GetW() const;
    void SetW(int32_t w);
    bool GetJournal() const;
    void SetJournal(bool journal);
    bool JournalIsSet() const;
    int32_t GetWTimeout() const;
    void SetWTimeout(int32_t timeout_ms);
    int64_t GetWTimeoutInt64() const;
    void SetWTimeoutInt64(int64_t timeout_ms);
    bool GetWMajority() const;
    void SetWMajority(bool wmajority);
    const char* GetWTag() const;
    int32_t SetWTag(const char* tag);
    bool IsAcknowledged() const;
    bool IsValid() const;
    bool IsDefault() const;
    bool AppendToOpts(BsonDocument& opts) const;

    void* RawWriteConcern();       // returns mongoc_write_concern_t*
    const void* RawWriteConcern() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ENGINE_API MongoReadConcern {
public:
    static constexpr const char* kLevelAvailable = "available";
    static constexpr const char* kLevelLocal = "local";
    static constexpr const char* kLevelMajority = "majority";
    static constexpr const char* kLevelLinearizable = "linearizable";
    static constexpr const char* kLevelSnapshot = "snapshot";

    MongoReadConcern();
    ~MongoReadConcern();

    MongoReadConcern(const MongoReadConcern&) = delete;
    MongoReadConcern& operator=(const MongoReadConcern&) = delete;
    MongoReadConcern(MongoReadConcern&& other) noexcept;
    MongoReadConcern& operator=(MongoReadConcern&& other) noexcept;

    MongoReadConcern Copy() const;

    const char* GetLevel() const;
    bool SetLevel(const char* level);
    bool IsDefault() const;
    bool AppendToOpts(BsonDocument& opts) const;

    void* RawReadConcern();       // returns mongoc_read_concern_t*
    const void* RawReadConcern() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
