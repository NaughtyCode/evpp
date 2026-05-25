#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// RAII wrapper around bson_t (128 bytes, stack-allocatable inline storage).
// Default-constructs empty, append fields, then pass to mongo operations.
//
// Usage:
//   BsonDocument doc;
//   doc.AppendInt32("key", 42);
//   doc.AppendUtf8("name", "value");
//   char* json = doc.AsJson();
class ENGINE_API BsonDocument {
public:
    BsonDocument();
    explicit BsonDocument(const uint8_t* data, size_t length);
    ~BsonDocument();

    // Non-copyable due to internal bson_t flags; use Copy().
    BsonDocument(const BsonDocument&) = delete;
    BsonDocument& operator=(const BsonDocument&) = delete;

    BsonDocument(BsonDocument&& other) noexcept;
    BsonDocument& operator=(BsonDocument&& other) noexcept;

    // Deep copy.
    BsonDocument Copy() const;

    // Reinitialize as an empty document.
    void Clear();

    // ── Append fields ───────────────────────────────────────────────
    // All return true on success.

    bool AppendDouble(const char* key, double value);
    bool AppendUtf8(const char* key, const char* value);
    bool AppendUtf8(const char* key, std::string_view value);
    bool AppendDocument(const char* key, const BsonDocument& subdoc);
    bool AppendArray(const char* key, const BsonDocument& array);
    bool AppendBinary(const char* key, int subtype, const uint8_t* data, uint32_t length);
    bool AppendBool(const char* key, bool value);
    bool AppendOid(const char* key, const MongoOid& oid);
    bool AppendInt32(const char* key, int32_t value);
    bool AppendInt64(const char* key, int64_t value);
    bool AppendDateTime(const char* key, int64_t msec_since_epoch);
    bool AppendNull(const char* key);
    bool AppendTimestamp(const char* key, uint32_t timestamp, uint32_t increment);

    // ── Sub-document building ───────────────────────────────────────
    bool AppendDocumentBegin(const char* key, BsonDocument* subdoc);
    static bool AppendDocumentEnd(BsonDocument* parent, BsonDocument* subdoc);
    bool AppendArrayBegin(const char* key, BsonDocument* array);
    static bool AppendArrayEnd(BsonDocument* parent, BsonDocument* array);

    // ── Query / access ──────────────────────────────────────────────
    uint32_t CountKeys() const;
    bool HasField(const char* key) const;
    bool Equal(const BsonDocument& other) const;
    int Compare(const BsonDocument& other) const;
    bool Concat(const BsonDocument& src);

    // ── Serialization ───────────────────────────────────────────────
    const uint8_t* GetData() const;
    uint32_t GetLength() const;

    char* AsCanonicalExtendedJson(size_t* length) const;
    char* AsRelaxedExtendedJson(size_t* length) const;
    char* AsJson(size_t* length) const;
    std::string ToJson() const;  // returns AsJson() as std::string, caller owns

    // ── Static initializers ─────────────────────────────────────────
    static BsonDocument NewFromJson(const char* json, size_t len);
    static BsonDocument NewFromJson(const uint8_t* data, size_t len);

    // ── Internal access (database/mongo/ layer only) ────────────────
    void* RawBson();         // returns bson_t*
    const void* RawBson() const;

private:
    // bson_t is 128 bytes. We store it inline so default-construction
    // is cheap and avoids heap allocation.
    alignas(8) char storage_[128];
};

// Iterator for traversing a BSON document's fields.
class ENGINE_API BsonIter {
public:
    BsonIter();
    explicit BsonIter(const BsonDocument& doc);
    ~BsonIter() = default;

    bool Next();
    bool Find(const char* key);
    const char* Key() const;

    // Type query
    int Type() const;

    // Typed value accessors — call the right one for the field type.
    double       AsDouble() const;
    int32_t      AsInt32() const;
    int64_t      AsInt64() const;
    const char*  AsUtf8(uint32_t* length) const;
    bool         AsBool() const;
    MongoOid     AsOid() const;
    int64_t      AsDateTime() const;
    void         AsBinary(int* subtype, uint32_t* length, const uint8_t** data) const;
    void         AsDocument(uint32_t* length, const uint8_t** data) const;

    // Recursion into sub-documents
    BsonIter Recurse() const;

    // Internal access
    void* RawIter();       // returns bson_iter_t*
    const void* RawIter() const;

private:
    alignas(8) char storage_[160]; // sizeof(bson_iter_t)
};

// Builder for BSON arrays (newer API style).
class ENGINE_API BsonArrayBuilder {
public:
    BsonArrayBuilder();
    ~BsonArrayBuilder();

    BsonArrayBuilder(const BsonArrayBuilder&) = delete;
    BsonArrayBuilder& operator=(const BsonArrayBuilder&) = delete;

    bool AppendDouble(double value);
    bool AppendUtf8(const char* value);
    bool AppendInt32(int32_t value);
    bool AppendInt64(int64_t value);
    bool AppendBool(bool value);
    bool AppendOid(const MongoOid& oid);

    // Finalize into a document (writes array into the given BsonDocument).
    bool Build(BsonDocument* out);

private:
    void* ptr_; // bson_array_builder_t*
};

} // namespace mongo
} // namespace engine
