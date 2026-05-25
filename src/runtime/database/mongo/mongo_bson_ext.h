#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// JSON serialization mode (mirrors bson_json_mode_t).
enum class BsonJsonMode : int {
    kLegacy = 0,
    kCanonical = 1,
    kRelaxed = 2,
};

// BSON type enum (mirrors bson_type_t).
enum class BsonType : uint8_t {
    kEOD = 0x00,
    kDouble = 0x01,
    kUtf8 = 0x02,
    kDocument = 0x03,
    kArray = 0x04,
    kBinary = 0x05,
    kUndefined = 0x06,
    kOid = 0x07,
    kBool = 0x08,
    kDateTime = 0x09,
    kNull = 0x0A,
    kRegex = 0x0B,
    kDBPointer = 0x0C,
    kCode = 0x0D,
    kSymbol = 0x0E,
    kCodeWithScope = 0x0F,
    kInt32 = 0x10,
    kTimestamp = 0x11,
    kInt64 = 0x12,
    kMaxkey = 0x7F,
    kMinkey = 0xFF,
    kDecimal128 = 0x13,
};

// Thin wrapper around bson_context_t.
class ENGINE_API BsonContext {
public:
    // Creates a new context (not the default shared one).
    static BsonContext New();
    // Returns the default context (thread-safe, shared).
    static const BsonContext& Default();

    BsonContext();
    ~BsonContext();
    BsonContext(const BsonContext&) = delete;
    BsonContext& operator=(const BsonContext&) = delete;
    BsonContext(BsonContext&&) noexcept;
    BsonContext& operator=(BsonContext&&) noexcept;

    void* Raw(); // returns bson_context_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Thin wrapper around bson_string_t for building strings incrementally.
class ENGINE_API BsonString {
public:
    BsonString();
    explicit BsonString(const char* str);
    ~BsonString();

    BsonString(const BsonString&) = delete;
    BsonString& operator=(const BsonString&) = delete;
    BsonString(BsonString&&) noexcept;
    BsonString& operator=(BsonString&&) noexcept;

    void Append(const char* str);
    void AppendPrintf(const char* format, ...);
    const char* GetString() const;
    size_t GetLength() const;
    bool Empty() const;
    void Truncate(size_t len);

    void* Raw(); // returns bson_string_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Reads JSON text and produces BSON documents.
//
// Usage:
//   BsonJsonReader reader;
//   reader.InitFromFd(fd);
//   BsonDocument doc;
//   bool ok = reader.Read(&doc);
//   reader.Destroy();
class ENGINE_API BsonJsonReader {
public:
    // Create from a file descriptor or memory buffer.
    static BsonJsonReader NewFromFd(int fd, bool close_on_destroy);
    static BsonJsonReader NewFromData(const uint8_t* data, size_t length);

    BsonJsonReader();
    ~BsonJsonReader();

    BsonJsonReader(const BsonJsonReader&) = delete;
    BsonJsonReader& operator=(const BsonJsonReader&) = delete;
    BsonJsonReader(BsonJsonReader&&) noexcept;
    BsonJsonReader& operator=(BsonJsonReader&&) noexcept;

    void Destroy();

    bool Read(BsonDocument* out, MongoError* error = nullptr);
    const char* ErrorDescription() const;

    void* Raw(); // returns bson_json_reader_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Incremental BSON binary reader (reads bson_t from a memory buffer).
class ENGINE_API BsonReader {
public:
    static BsonReader NewFromData(const uint8_t* data, size_t length);

    BsonReader();
    ~BsonReader();

    BsonReader(const BsonReader&) = delete;
    BsonReader& operator=(const BsonReader&) = delete;
    BsonReader(BsonReader&&) noexcept;
    BsonReader& operator=(BsonReader&&) noexcept;

    void Destroy();

    const void* Read(bool* reached_eof) const; // returns const bson_t*
    bool Read(BsonDocument* out, MongoError* error = nullptr);
    void SetData(const uint8_t* data, size_t length);

    void* Raw(); // returns bson_reader_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Incremental BSON binary writer.
class ENGINE_API BsonWriter {
public:
    BsonWriter();
    ~BsonWriter();

    BsonWriter(const BsonWriter&) = delete;
    BsonWriter& operator=(const BsonWriter&) = delete;
    BsonWriter(BsonWriter&&) noexcept;
    BsonWriter& operator=(BsonWriter&&) noexcept;

    void Destroy();

    // Begin a new document or array.
    bool Begin(const void* raw_bson = nullptr);     // bson_t* or nullptr for empty
    bool BeginDocument(BsonDocument* doc);
    bool BeginArray(BsonDocument* array);

    // End the current document/array — finalizes it into `out`.
    void End(BsonDocument* out);

    bool Rollback();

    // Get the current buffer (not yet finalized).
    const uint8_t* GetBuffer(size_t* length) const;

    void* Raw(); // returns bson_writer_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Clock utilities (wraps bson-clock.h).
class ENGINE_API BsonClock {
public:
    static int64_t GetTimeNs();
    static int64_t GetDateTime();
};

// UTF-8 validation utilities (wraps bson-utf8.h).
class ENGINE_API BsonUtf8 {
public:
    static bool Validate(const char* str, size_t length, bool allow_null = false);
    static char* EscapeForJson(const char* str, size_t length);
};

// JSON serialization options (wraps bson_json_opts_t).
class ENGINE_API BsonJsonOpts {
public:
    BsonJsonOpts(BsonJsonMode mode = BsonJsonMode::kLegacy, int32_t max_len = -1);
    ~BsonJsonOpts();

    BsonJsonOpts(const BsonJsonOpts&) = delete;
    BsonJsonOpts& operator=(const BsonJsonOpts&) = delete;
    BsonJsonOpts(BsonJsonOpts&&) noexcept;
    BsonJsonOpts& operator=(BsonJsonOpts&&) noexcept;

    void SetOutermostArray(bool is_outermost_array);

    void* Raw(); // returns bson_json_opts_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// RAII wrapper around bson_value_t for value copy/destroy lifecycle.
class ENGINE_API BsonValue {
public:
    BsonValue();
    ~BsonValue();

    BsonValue(const BsonValue& other);
    BsonValue& operator=(const BsonValue& other);
    BsonValue(BsonValue&&) noexcept;
    BsonValue& operator=(BsonValue&&) noexcept;

    void Copy(const BsonValue& src);
    void Destroy();

    void* Raw();       // returns bson_value_t*
    const void* Raw() const;

private:
    // bson_value_t is a tagged union; inline storage
    alignas(8) char storage_[64];
};

// Key constants for well-known BSON keys (wraps bson-keys.h).
namespace BsonKeys {
    size_t Uint32ToString(uint32_t value, const char** strptr, char* str, size_t size);

    extern const char* kOid;
    extern const char* kSet;
    extern const char* kUnset;
    extern const char* kInc;
    extern const char* kPush;
    extern const char* kPull;
    extern const char* kGte;
    extern const char* kLte;
    extern const char* kGt;
    extern const char* kLt;
    extern const char* kNe;
    extern const char* kIn;
    extern const char* kNin;
    extern const char* kExists;
    extern const char* kRegex;
    extern const char* kOptions;
    extern const char* kAnd;
    extern const char* kOr;
    extern const char* kNor;
    extern const char* kNot;
    extern const char* kSize;
    extern const char* kType;
    extern const char* kAll;
    extern const char* kElemMatch;
    extern const char* kSlice;
    extern const char* kSearch;
    extern const char* kLanguage;
    extern const char* kText;
    extern const char* kComment;
    extern const char* kBits;
    extern const char* kNearSphere;
    extern const char* kMaxDistance;
    extern const char* kMinDistance;
    extern const char* kGeometry;
    extern const char* kUniqueDocs;
}

} // namespace mongo
} // namespace engine
