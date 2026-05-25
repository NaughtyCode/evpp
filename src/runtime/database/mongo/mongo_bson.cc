#include "runtime/database/mongo/mongo_bson.h"

// mongo-c-driver headers — only included in .cc files, never in .h
#include <bson/bson.h>

#include <cstring>

#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_oid.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// Static assertions: verify our inline storage sizes match the C types.
// ═══════════════════════════════════════════════════════════════════════
static_assert(sizeof(BsonDocument) == sizeof(bson_t),
              "BsonDocument storage must match bson_t size");
static_assert(alignof(BsonDocument) == alignof(bson_t),
              "BsonDocument alignment must match bson_t");
static_assert(sizeof(BsonIter) >= sizeof(bson_iter_t),
              "BsonIter storage must be >= bson_iter_t size");

// ═══════════════════════════════════════════════════════════════════════
// BsonDocument
// ═══════════════════════════════════════════════════════════════════════

BsonDocument::BsonDocument() {
    bson_init(static_cast<bson_t*>(RawBson()));
}

BsonDocument::BsonDocument(const uint8_t* data, size_t length) {
    auto* b = static_cast<bson_t*>(RawBson());
    bson_init_static(b, data, length);
}

BsonDocument::~BsonDocument() {
    bson_destroy(static_cast<bson_t*>(RawBson()));
}

BsonDocument::BsonDocument(BsonDocument&& other) noexcept {
    bson_steal(static_cast<bson_t*>(RawBson()),
               static_cast<bson_t*>(other.RawBson()));
}

BsonDocument& BsonDocument::operator=(BsonDocument&& other) noexcept {
    if (this != &other) {
        bson_destroy(static_cast<bson_t*>(RawBson()));
        bson_steal(static_cast<bson_t*>(RawBson()),
                   static_cast<bson_t*>(other.RawBson()));
    }
    return *this;
}

BsonDocument BsonDocument::Copy() const {
    BsonDocument result;
    bson_copy_to(static_cast<const bson_t*>(RawBson()),
                 static_cast<bson_t*>(result.RawBson()));
    return result;
}

void BsonDocument::Clear() {
    bson_destroy(static_cast<bson_t*>(RawBson()));
    bson_init(static_cast<bson_t*>(RawBson()));
}

bool BsonDocument::AppendDouble(const char* key, double value) {
    return bson_append_double(static_cast<bson_t*>(RawBson()), key, -1, value);
}

bool BsonDocument::AppendUtf8(const char* key, const char* value) {
    return bson_append_utf8(static_cast<bson_t*>(RawBson()), key, -1, value, -1);
}

bool BsonDocument::AppendUtf8(const char* key, std::string_view value) {
    return bson_append_utf8(static_cast<bson_t*>(RawBson()), key, -1, value.data(), static_cast<int>(value.size()));
}

bool BsonDocument::AppendDocument(const char* key, const BsonDocument& subdoc) {
    return bson_append_document(static_cast<bson_t*>(RawBson()), key, -1,
                                static_cast<const bson_t*>(subdoc.RawBson()));
}

bool BsonDocument::AppendArray(const char* key, const BsonDocument& array) {
    return bson_append_array(static_cast<bson_t*>(RawBson()), key, -1,
                             static_cast<const bson_t*>(array.RawBson()));
}

bool BsonDocument::AppendBinary(const char* key, int subtype,
                                 const uint8_t* data, uint32_t length) {
    return bson_append_binary(static_cast<bson_t*>(RawBson()), key, -1,
                              static_cast<bson_subtype_t>(subtype), data, length);
}

bool BsonDocument::AppendBool(const char* key, bool value) {
    return bson_append_bool(static_cast<bson_t*>(RawBson()), key, -1, value);
}

bool BsonDocument::AppendOid(const char* key, const MongoOid& oid) {
    return bson_append_oid(static_cast<bson_t*>(RawBson()), key, -1,
                           reinterpret_cast<const bson_oid_t*>(oid.data()));
}

bool BsonDocument::AppendInt32(const char* key, int32_t value) {
    return bson_append_int32(static_cast<bson_t*>(RawBson()), key, -1, value);
}

bool BsonDocument::AppendInt64(const char* key, int64_t value) {
    return bson_append_int64(static_cast<bson_t*>(RawBson()), key, -1, value);
}

bool BsonDocument::AppendDateTime(const char* key, int64_t msec_since_epoch) {
    return bson_append_date_time(static_cast<bson_t*>(RawBson()), key, -1, msec_since_epoch);
}

bool BsonDocument::AppendNull(const char* key) {
    return bson_append_null(static_cast<bson_t*>(RawBson()), key, -1);
}

bool BsonDocument::AppendTimestamp(const char* key, uint32_t timestamp, uint32_t increment) {
    return bson_append_timestamp(static_cast<bson_t*>(RawBson()), key, -1, timestamp, increment);
}

bool BsonDocument::AppendDocumentBegin(const char* key, BsonDocument* subdoc) {
    return bson_append_document_begin(static_cast<bson_t*>(RawBson()), key, -1,
                                      static_cast<bson_t*>(subdoc->RawBson()));
}

bool BsonDocument::AppendDocumentEnd(BsonDocument* parent, BsonDocument* subdoc) {
    return bson_append_document_end(static_cast<bson_t*>(parent->RawBson()),
                                    static_cast<bson_t*>(subdoc->RawBson()));
}

bool BsonDocument::AppendArrayBegin(const char* key, BsonDocument* array) {
    return bson_append_array_begin(static_cast<bson_t*>(RawBson()), key, -1,
                                   static_cast<bson_t*>(array->RawBson()));
}

bool BsonDocument::AppendArrayEnd(BsonDocument* parent, BsonDocument* array) {
    return bson_append_array_end(static_cast<bson_t*>(parent->RawBson()),
                                 static_cast<bson_t*>(array->RawBson()));
}

uint32_t BsonDocument::CountKeys() const {
    return bson_count_keys(static_cast<const bson_t*>(RawBson()));
}

bool BsonDocument::HasField(const char* key) const {
    return bson_has_field(static_cast<const bson_t*>(RawBson()), key);
}

bool BsonDocument::Equal(const BsonDocument& other) const {
    return bson_equal(static_cast<const bson_t*>(RawBson()),
                      static_cast<const bson_t*>(other.RawBson()));
}

int BsonDocument::Compare(const BsonDocument& other) const {
    return bson_compare(static_cast<const bson_t*>(RawBson()),
                        static_cast<const bson_t*>(other.RawBson()));
}

bool BsonDocument::Concat(const BsonDocument& src) {
    return bson_concat(static_cast<bson_t*>(RawBson()),
                       static_cast<const bson_t*>(src.RawBson()));
}

const uint8_t* BsonDocument::GetData() const {
    return bson_get_data(static_cast<const bson_t*>(RawBson()));
}

uint32_t BsonDocument::GetLength() const {
    return static_cast<const bson_t*>(RawBson())->len;
}

char* BsonDocument::AsCanonicalExtendedJson(size_t* length) const {
    return bson_as_canonical_extended_json(static_cast<const bson_t*>(RawBson()), length);
}

char* BsonDocument::AsRelaxedExtendedJson(size_t* length) const {
    return bson_as_relaxed_extended_json(static_cast<const bson_t*>(RawBson()), length);
}

char* BsonDocument::AsJson(size_t* length) const {
    return bson_as_relaxed_extended_json(static_cast<const bson_t*>(RawBson()), length);
}

std::string BsonDocument::ToJson() const {
    size_t len;
    char* json = bson_as_relaxed_extended_json(static_cast<const bson_t*>(RawBson()), &len);
    if (!json) return "{}";
    std::string result(json, len);
    bson_free(json);
    return result;
}

BsonDocument BsonDocument::NewFromJson(const char* json, size_t len) {
    bson_error_t err;
    bson_t* b = bson_new_from_json(reinterpret_cast<const uint8_t*>(json), static_cast<ssize_t>(len), &err);
    BsonDocument result;
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

BsonDocument BsonDocument::NewFromJson(const uint8_t* data, size_t len) {
    bson_error_t err;
    bson_t* b = bson_new_from_json(data, static_cast<ssize_t>(len), &err);
    BsonDocument result;
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

void* BsonDocument::RawBson() { return static_cast<void*>(storage_); }
const void* BsonDocument::RawBson() const { return static_cast<const void*>(storage_); }

// ═══════════════════════════════════════════════════════════════════════
// BsonIter
// ═══════════════════════════════════════════════════════════════════════

BsonIter::BsonIter() {
    std::memset(storage_, 0, sizeof(storage_));
}

BsonIter::BsonIter(const BsonDocument& doc) {
    bson_iter_init(static_cast<bson_iter_t*>(RawIter()),
                   static_cast<const bson_t*>(doc.RawBson()));
}

bool BsonIter::Next() {
    return bson_iter_next(static_cast<bson_iter_t*>(RawIter()));
}

bool BsonIter::Find(const char* key) {
    return bson_iter_find(static_cast<bson_iter_t*>(RawIter()), key);
}

const char* BsonIter::Key() const {
    return bson_iter_key(static_cast<const bson_iter_t*>(RawIter()));
}

int BsonIter::Type() const {
    return static_cast<int>(bson_iter_type(static_cast<const bson_iter_t*>(RawIter())));
}

double BsonIter::AsDouble() const {
    return bson_iter_double(static_cast<const bson_iter_t*>(RawIter()));
}

int32_t BsonIter::AsInt32() const {
    return bson_iter_int32(static_cast<const bson_iter_t*>(RawIter()));
}

int64_t BsonIter::AsInt64() const {
    return bson_iter_int64(static_cast<const bson_iter_t*>(RawIter()));
}

const char* BsonIter::AsUtf8(uint32_t* length) const {
    return bson_iter_utf8(static_cast<const bson_iter_t*>(RawIter()), length);
}

bool BsonIter::AsBool() const {
    return bson_iter_bool(static_cast<const bson_iter_t*>(RawIter()));
}

MongoOid BsonIter::AsOid() const {
    const bson_oid_t* oid = bson_iter_oid(static_cast<const bson_iter_t*>(RawIter()));
    MongoOid result;
    if (oid) {
        result.SetBytes(oid->bytes);
    }
    return result;
}

int64_t BsonIter::AsDateTime() const {
    return bson_iter_date_time(static_cast<const bson_iter_t*>(RawIter()));
}

void BsonIter::AsBinary(int* subtype, uint32_t* length, const uint8_t** data) const {
    bson_subtype_t st;
    bson_iter_binary(static_cast<const bson_iter_t*>(RawIter()), &st, length, data);
    *subtype = static_cast<int>(st);
}

void BsonIter::AsDocument(uint32_t* length, const uint8_t** data) const {
    bson_iter_document(static_cast<const bson_iter_t*>(RawIter()), length, data);
}

BsonIter BsonIter::Recurse() const {
    BsonIter child;
    bson_iter_recurse(static_cast<const bson_iter_t*>(RawIter()),
                      static_cast<bson_iter_t*>(child.RawIter()));
    return child;
}

void* BsonIter::RawIter() { return static_cast<void*>(storage_); }

const void* BsonIter::RawIter() const { return static_cast<const void*>(storage_); }

// ═══════════════════════════════════════════════════════════════════════
// BsonArrayBuilder
// ═══════════════════════════════════════════════════════════════════════

BsonArrayBuilder::BsonArrayBuilder()
    : ptr_(bson_array_builder_new()) {}

BsonArrayBuilder::~BsonArrayBuilder() {
    if (ptr_) {
        bson_array_builder_destroy(static_cast<bson_array_builder_t*>(ptr_));
    }
}

bool BsonArrayBuilder::AppendDouble(double value) {
    return bson_array_builder_append_double(
        static_cast<bson_array_builder_t*>(ptr_), value);
}

bool BsonArrayBuilder::AppendUtf8(const char* value) {
    return bson_array_builder_append_utf8(
        static_cast<bson_array_builder_t*>(ptr_), value, -1);
}

bool BsonArrayBuilder::AppendInt32(int32_t value) {
    return bson_array_builder_append_int32(
        static_cast<bson_array_builder_t*>(ptr_), value);
}

bool BsonArrayBuilder::AppendInt64(int64_t value) {
    return bson_array_builder_append_int64(
        static_cast<bson_array_builder_t*>(ptr_), value);
}

bool BsonArrayBuilder::AppendBool(bool value) {
    return bson_array_builder_append_bool(
        static_cast<bson_array_builder_t*>(ptr_), value);
}

bool BsonArrayBuilder::AppendOid(const MongoOid& oid) {
    return bson_array_builder_append_oid(
        static_cast<bson_array_builder_t*>(ptr_),
        reinterpret_cast<const bson_oid_t*>(oid.data()));
}

bool BsonArrayBuilder::Build(BsonDocument* out) {
    return bson_array_builder_build(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<bson_t*>(out->RawBson()));
}

} // namespace mongo
} // namespace engine
