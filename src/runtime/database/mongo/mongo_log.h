#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Log level (mirrors mongoc_log_level_t).
enum class MongoLogLevel : int {
    kError = 0,
    kCritical = 1,
    kWarning = 2,
    kMessage = 3,
    kInfo = 4,
    kDebug = 5,
    kTrace = 6,
};

// Structured log level (mirrors mongoc_structured_log_level_t).
enum class MongoStructuredLogLevel : int {
    kEmergency = 0,
    kAlert = 1,
    kCritical = 2,
    kError = 3,
    kWarning = 4,
    kNotice = 5,
    kInfo = 6,
    kDebug = 7,
    kTrace = 8,
};

// Component for structured logging (mirrors mongoc_structured_log_component_t).
enum class MongoStructuredLogComponent : int {
    kCommand = 0,
    kTopology = 1,
    kServerSelection = 2,
    kConnection = 3,
};

// Simple log API (wraps mongoc-log.h).
class ENGINE_API MongoLog {
public:
    // Set a custom log handler. Pass nullptr to restore default.
    using LogFunc = std::function<void(MongoLogLevel level, const char* domain, const char* message)>;
    static void SetHandler(LogFunc handler);

    // Log a message at the given level.
    static void Log(MongoLogLevel level, const char* domain, const char* format, ...);

    static const char* LevelToString(MongoLogLevel level);

    static void TraceEnable();
    static void TraceDisable();

    static void DefaultHandler(MongoLogLevel level, const char* domain, const char* message);
};

// Structured log entry accessor (read-only wrapper around mongoc_structured_log_entry_t).
class ENGINE_API MongoStructuredLogEntry {
public:
    explicit MongoStructuredLogEntry(const void* raw_entry);

    // Returns a BSON document representation of the entry.
    void MessageAsBson(BsonDocument* out) const;

    MongoStructuredLogLevel GetLevel() const;
    MongoStructuredLogComponent GetComponent() const;
    const char* GetMessageString() const;

private:
    const void* entry_;
};

// Structured log options (wraps mongoc_structured_log_opts_t).
class ENGINE_API MongoStructuredLogOpts {
public:
    MongoStructuredLogOpts();
    ~MongoStructuredLogOpts();

    MongoStructuredLogOpts(const MongoStructuredLogOpts&) = delete;
    MongoStructuredLogOpts& operator=(const MongoStructuredLogOpts&) = delete;
    MongoStructuredLogOpts(MongoStructuredLogOpts&&) = delete;
    MongoStructuredLogOpts& operator=(MongoStructuredLogOpts&&) = delete;

    using LogFunc = std::function<void(const MongoStructuredLogEntry& entry)>;
    void SetHandler(LogFunc handler);

    bool SetMaxLevelForComponent(MongoStructuredLogComponent component, MongoStructuredLogLevel level);
    bool SetMaxLevelForAllComponents(MongoStructuredLogLevel level);
    bool SetMaxLevelsFromEnv();
    MongoStructuredLogLevel GetMaxLevelForComponent(MongoStructuredLogComponent component) const;

    size_t GetMaxDocumentLength() const;
    bool SetMaxDocumentLength(size_t max_len);
    bool SetMaxDocumentLengthFromEnv();

    // Name <-> enum helpers
    static const char* GetLevelName(MongoStructuredLogLevel level);
    static bool GetNamedLevel(const char* name, MongoStructuredLogLevel* out);
    static const char* GetComponentName(MongoStructuredLogComponent component);
    static bool GetNamedComponent(const char* name, MongoStructuredLogComponent* out);

    const void* Raw() const;
    void* Raw();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
