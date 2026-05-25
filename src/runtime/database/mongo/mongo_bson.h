#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// 16-byte IEEE 754 decimal128 floating-point value.
// Binary-compatible with bson_decimal128_t.
class ENGINE_API MongoDecimal128 {
public:
    MongoDecimal128() : high_(0), low_(0) {}
    explicit MongoDecimal128(uint64_t high, uint64_t low) : high_(high), low_(low) {}

    uint64_t high() const { return high_; }
    uint64_t low()  const { return low_; }

    bool FromString(const char* str);
    bool FromStringLen(const char* str, int len);
    std::string ToString() const;

private:
    uint64_t high_;
    uint64_t low_;
};
static_assert(sizeof(MongoDecimal128) == 16, "MongoDecimal128 must be 16 bytes");

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
    bool AppendCode(const char* key, const char* javascript);
    bool AppendCodeWithScope(const char* key, const char* javascript, const BsonDocument& scope);
    bool AppendRegex(const char* key, const char* regex, const char* options);
    bool AppendSymbol(const char* key, const char* symbol);
    bool AppendUndefined(const char* key);
    bool AppendMinkey(const char* key);
    bool AppendMaxkey(const char* key);
    bool AppendDBPointer(const char* key, const char* collection, const MongoOid& oid);
    bool AppendTimeT(const char* key, time_t value);
    bool AppendNowUtc(const char* key);
    bool AppendDecimal128(const char* key, const MongoDecimal128& value);
    bool AppendValue(const char* key, const void* bson_value);
    bool AppendIter(const char* key, const BsonIter& iter);
    bool AppendBinaryUninit(const char* key, int subtype, uint32_t len, uint8_t** data_out);
    bool AppendArrayFromVector(const char* key, const BsonIter& iter);

    // ── Sub-document building ───────────────────────────────────────
    bool AppendDocumentBegin(const char* key, BsonDocument* subdoc);
    static bool AppendDocumentEnd(BsonDocument* parent, BsonDocument* subdoc);
    bool AppendArrayBegin(const char* key, BsonDocument* array);
    static bool AppendArrayEnd(BsonDocument* parent, BsonDocument* array);

    // ── Query / access ──────────────────────────────────────────────
    uint32_t CountKeys() const;
    bool HasField(const char* key) const;
    bool Empty() const;
    bool Equal(const BsonDocument& other) const;
    int Compare(const BsonDocument& other) const;
    bool Concat(const BsonDocument& src);

    // ── Serialization ───────────────────────────────────────────────
    const uint8_t* GetData() const;
    uint32_t GetLength() const;

    char* AsCanonicalExtendedJson(size_t* length) const;
    char* AsRelaxedExtendedJson(size_t* length) const;
    char* AsJson(size_t* length) const;
    char* AsJsonWithOpts(size_t* length, const void* opts) const; // bson_json_opts_t*
    std::string ToJson() const;  // returns AsJson() as std::string, caller owns

    static char* ArrayAsCanonicalExtendedJson(const BsonDocument& array, size_t* length);
    static char* ArrayAsRelaxedExtendedJson(const BsonDocument& array, size_t* length);

    // ── Static initializers ─────────────────────────────────────────
    static BsonDocument NewFromJson(const char* json, size_t len);
    static BsonDocument NewFromJson(const uint8_t* data, size_t len);
    static BsonDocument NewFromData(const uint8_t* data, size_t length);
    static BsonDocument NewFromBuffer(uint8_t** buf, size_t* buf_len,
                                       void* realloc_func, void* realloc_func_ctx);
    static BsonDocument SizedNew(size_t size);

    // ── Validation ──────────────────────────────────────────────────
    bool Validate(MongoError* error = nullptr) const;
    void Reinit(); // reinitialize as an empty document

    // ── Steal (move bson_t buffer; src is left empty) ───────────────
    static void Steal(BsonDocument& dst, BsonDocument& src);

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
    const char*  AsCode() const;
    void         AsCodeWithScope(uint32_t* code_length, const char** code, BsonDocument* scope) const;
    void         AsRegex(const char** regex, const char** options) const;
    const char*  AsSymbol() const;
    void         AsTimestamp(uint32_t* timestamp, uint32_t* increment) const;
    time_t       AsTimeT() const;
    bool         AsDecimal128(MongoDecimal128* dec) const;

    // Find variants
    bool FindCase(const char* key);
    bool FindDescendant(const char* dotkey, BsonIter* descendant);
    bool FindWLen(const char* key, int keylen);

    // Init-and-find (initialize iterator and find key in one call)
    bool InitFind(const BsonDocument& doc, const char* key);
    bool InitFindCase(const BsonDocument& doc, const char* key);
    bool InitFindWLen(const BsonDocument& doc, const char* key, int keylen);
    bool InitFromData(const uint8_t* data, size_t length);
    bool InitFromDataAtOffset(const uint8_t* data, size_t length, uint32_t offset, uint32_t keylen);
    const char* KeyUnsafe() const;
    uint32_t KeyLen() const;
    char* DupUtf8(uint32_t* length) const;

    // Type-specific helpers
    int BinarySubtype() const;
    static bool BinaryEqual(const BsonIter& a, const BsonIter& b);

    // Timeval
    void AsTimeval(void* tv) const; // struct timeval*

    // DBPointer type access
    void AsDBPointer(uint32_t* collection_len, const char** collection, const void** oid) const;

    // Visit all fields with a visitor callback
    bool VisitAll(const void* visitor, void* data);

    // Offset (byte position in the document)
    uint32_t Offset() const;

    // Get the raw bson_value_t for the current element
    const void* Value() const;

    // Overwrite current element's value (must be at correct position)
    bool OverwriteInt32(int32_t value);
    bool OverwriteInt64(int64_t value);
    bool OverwriteDouble(double value);
    bool OverwriteDecimal128(const MongoDecimal128& value);
    bool OverwriteBool(bool value);
    bool OverwriteOid(const MongoOid& value);
    bool OverwriteTimestamp(uint32_t timestamp, uint32_t increment);
    bool OverwriteDateTime(int64_t value);
    bool OverwriteBinary(int subtype, uint32_t* binary_len, uint8_t** binary);

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
    bool AppendNull();
    bool AppendDateTime(int64_t msec_since_epoch);
    bool AppendTimestamp(uint32_t timestamp, uint32_t increment);
    bool AppendDocument(const BsonDocument& doc);
    bool AppendArray(const BsonDocument& array);
    bool AppendBinary(int subtype, const uint8_t* data, uint32_t length);
    bool AppendRegex(const char* regex, const char* options);
    bool AppendCode(const char* javascript);
    bool AppendCodeWithScope(const char* javascript, const BsonDocument& scope);
    bool AppendIter(const BsonIter& iter);
    bool AppendValue(const void* bson_value);
    bool AppendMinkey();
    bool AppendMaxkey();
    bool AppendUndefined();
    bool AppendSymbol(const char* value);
    bool AppendDBPointer(const char* collection, const MongoOid& oid);
    bool AppendTimeT(time_t value);
    bool AppendNowUtc();
    bool AppendDecimal128(const MongoDecimal128& value);

    // Sub-document building within the builder
    bool AppendDocumentBegin(BsonDocument* subdoc);
    static bool AppendDocumentEnd(BsonArrayBuilder* builder, BsonDocument* subdoc);

    // Finalize into a document (writes array into the given BsonDocument).
    bool Build(BsonDocument* out);

private:
    void* ptr_; // bson_array_builder_t*
};

} // namespace mongo
} // namespace engine
