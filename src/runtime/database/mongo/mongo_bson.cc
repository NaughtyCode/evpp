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

bool BsonDocument::CopyTo(BsonDocument& dst) const {
    bson_copy_to(static_cast<const bson_t*>(RawBson()),
                 static_cast<bson_t*>(dst.RawBson()));
    return true;
}

bool BsonDocument::CopyToExcludingNoinit(BsonDocument& dst, const char* first_exclude, ...) {
    va_list args;
    va_start(args, first_exclude);
    bson_copy_to_excluding_noinit(static_cast<const bson_t*>(RawBson()),
                                   static_cast<bson_t*>(dst.RawBson()),
                                   first_exclude, args);
    va_end(args);
    return true;
}

bool BsonDocument::CopyToExcludingNoinitVa(BsonDocument& dst, const char* first_exclude, void* args) {
    bson_copy_to_excluding_noinit_va(static_cast<const bson_t*>(RawBson()),
                                      static_cast<bson_t*>(dst.RawBson()),
                                      first_exclude,
                                      *static_cast<va_list*>(args));
    return true;
}

bool BsonDocument::ReserveBuffer(uint32_t size) {
    return bson_reserve_buffer(static_cast<bson_t*>(RawBson()), size);
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

bool BsonDocument::AppendCode(const char* key, const char* javascript) {
    return bson_append_code(static_cast<bson_t*>(RawBson()), key, -1, javascript);
}

bool BsonDocument::AppendCodeWithScope(const char* key, const char* javascript, const BsonDocument& scope) {
    return bson_append_code_with_scope(static_cast<bson_t*>(RawBson()), key, -1, javascript,
                                       static_cast<const bson_t*>(scope.RawBson()));
}

bool BsonDocument::AppendRegex(const char* key, const char* regex, const char* options) {
    return bson_append_regex(static_cast<bson_t*>(RawBson()), key, -1, regex, options);
}

bool BsonDocument::AppendRegexWLen(const char* key, int keylen, const char* regex, const char* options) {
    return bson_append_regex_w_len(static_cast<bson_t*>(RawBson()), key, keylen, regex, -1, options);
}

bool BsonDocument::AppendSymbol(const char* key, const char* symbol) {
    return bson_append_symbol(static_cast<bson_t*>(RawBson()), key, -1, symbol, -1);
}

bool BsonDocument::AppendUndefined(const char* key) {
    return bson_append_undefined(static_cast<bson_t*>(RawBson()), key, -1);
}

bool BsonDocument::AppendMinkey(const char* key) {
    return bson_append_minkey(static_cast<bson_t*>(RawBson()), key, -1);
}

bool BsonDocument::AppendMaxkey(const char* key) {
    return bson_append_maxkey(static_cast<bson_t*>(RawBson()), key, -1);
}

bool BsonDocument::AppendDBPointer(const char* key, const char* collection, const MongoOid& oid) {
    return bson_append_dbpointer(static_cast<bson_t*>(RawBson()), key, -1, collection,
                                  reinterpret_cast<const bson_oid_t*>(oid.data()));
}

bool BsonDocument::AppendTimeT(const char* key, time_t value) {
    return bson_append_time_t(static_cast<bson_t*>(RawBson()), key, -1, value);
}

bool BsonDocument::AppendTimeval(const char* key, const void* tv) {
    return bson_append_timeval(static_cast<bson_t*>(RawBson()), key, -1,
                               const_cast<struct timeval*>(static_cast<const struct timeval*>(tv)));
}

bool BsonDocument::AppendNowUtc(const char* key) {
    return bson_append_now_utc(static_cast<bson_t*>(RawBson()), key, -1);
}

bool BsonDocument::AppendDecimal128(const char* key, const MongoDecimal128& value) {
    return bson_append_decimal128(static_cast<bson_t*>(RawBson()), key, -1,
                                   reinterpret_cast<const bson_decimal128_t*>(&value));
}

bool BsonDocument::AppendValue(const char* key, const void* bson_value) {
    return bson_append_value(static_cast<bson_t*>(RawBson()), key, -1,
                              static_cast<const bson_value_t*>(bson_value));
}

bool BsonDocument::AppendIter(const char* key, const BsonIter& iter) {
    return bson_append_iter(static_cast<bson_t*>(RawBson()), key, -1,
                             static_cast<const bson_iter_t*>(iter.RawIter()));
}

bool BsonDocument::AppendBinaryUninit(const char* key, int subtype, uint8_t** data_out,
                                        uint32_t len) {
    return bson_append_binary_uninit(static_cast<bson_t*>(RawBson()), key, -1,
        static_cast<bson_subtype_t>(subtype), data_out, len);
}

bool BsonDocument::AppendArrayFromVector(const char* key, const BsonIter& iter) {
    return bson_append_array_from_vector(static_cast<bson_t*>(RawBson()), key, -1,
        static_cast<const bson_iter_t*>(iter.RawIter()));
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

bool BsonDocument::AppendArrayUnsafeBegin(const char* key, BsonDocument* child) {
    return bson_append_array_unsafe_begin(static_cast<bson_t*>(RawBson()), key, -1,
                                          static_cast<bson_t*>(child->RawBson()));
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

bool BsonDocument::Empty() const {
    return bson_empty(static_cast<const bson_t*>(RawBson()));
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

char* BsonDocument::AsLegacyExtendedJson(size_t* length) const {
    return bson_as_legacy_extended_json(static_cast<const bson_t*>(RawBson()), length);
}

char* BsonDocument::AsJson(size_t* length) const {
    return bson_as_relaxed_extended_json(static_cast<const bson_t*>(RawBson()), length);
}

char* BsonDocument::AsJsonWithOpts(size_t* length, const void* opts) const {
    return bson_as_json_with_opts(static_cast<const bson_t*>(RawBson()), length,
        static_cast<const bson_json_opts_t*>(opts));
}

char* BsonDocument::ArrayAsCanonicalExtendedJson(const BsonDocument& array, size_t* length) {
    return bson_array_as_canonical_extended_json(
        static_cast<const bson_t*>(array.RawBson()), length);
}

char* BsonDocument::ArrayAsRelaxedExtendedJson(const BsonDocument& array, size_t* length) {
    return bson_array_as_relaxed_extended_json(
        static_cast<const bson_t*>(array.RawBson()), length);
}

char* BsonDocument::ArrayAsLegacyExtendedJson(const BsonDocument& array, size_t* length) {
    return bson_array_as_legacy_extended_json(
        static_cast<const bson_t*>(array.RawBson()), length);
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
    bson_t* b = bson_new_from_json(reinterpret_cast<const uint8_t*>(json), static_cast<int64_t>(len), &err);
    BsonDocument result;
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

BsonDocument BsonDocument::NewFromJson(const uint8_t* data, size_t len) {
    bson_error_t err;
    bson_t* b = bson_new_from_json(data, static_cast<int64_t>(len), &err);
    BsonDocument result;
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

BsonDocument BsonDocument::NewFromData(const uint8_t* data, size_t length) {
    BsonDocument result;
    bson_t* b = bson_new_from_data(data, length);
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

BsonDocument BsonDocument::NewFromBuffer(uint8_t** buf, size_t* buf_len,
                                           void* realloc_func, void* realloc_func_ctx) {
    BsonDocument result;
    bson_t* b = bson_new_from_buffer(buf, buf_len,
        reinterpret_cast<bson_realloc_func>(realloc_func), realloc_func_ctx);
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

BsonDocument BsonDocument::SizedNew(size_t size) {
    BsonDocument result;
    bson_t* b = bson_sized_new(size);
    if (b) {
        bson_destroy(static_cast<bson_t*>(result.RawBson()));
        bson_steal(static_cast<bson_t*>(result.RawBson()), b);
    }
    return result;
}

void BsonDocument::Steal(BsonDocument& dst, BsonDocument& src) {
    bson_destroy(static_cast<bson_t*>(dst.RawBson()));
    bson_steal(static_cast<bson_t*>(dst.RawBson()),
               static_cast<bson_t*>(src.RawBson()));
}

void BsonDocument::DestroyWithSteal(uint8_t** data, uint32_t* length, bool* reached_eof, void* raw_bson) {
    *data = bson_destroy_with_steal(static_cast<bson_t*>(raw_bson), true, length);
    if (reached_eof) {
        *reached_eof = false;
    }
}

bool BsonDocument::AppendArrayBuilderBegin(const char* key, void** builder_out) {
    return bson_append_array_builder_begin(static_cast<bson_t*>(RawBson()), key, -1,
                                            reinterpret_cast<bson_array_builder_t**>(builder_out));
}

bool BsonDocument::AppendArrayBuilderEnd(BsonDocument* parent, void* builder) {
    return bson_append_array_builder_end(static_cast<bson_t*>(parent->RawBson()),
                                          static_cast<bson_array_builder_t*>(builder));
}

bool BsonDocument::Validate(MongoError* error) const {
    bson_error_t err;
    bool ok = bson_validate_with_error(static_cast<const bson_t*>(RawBson()), BSON_VALIDATE_NONE, &err);
    if (!ok && error) {
        std::memcpy(error->RawError(), &err, sizeof(err));
    }
    return ok;
}

bool BsonDocument::ValidateWithErrorAndOffset(MongoError* error, size_t* offset) const {
    bson_error_t err;
    bool ok = bson_validate_with_error_and_offset(
        static_cast<const bson_t*>(RawBson()), BSON_VALIDATE_NONE, offset, &err);
    if (!ok && error) {
        std::memcpy(error->RawError(), &err, sizeof(err));
    }
    return ok;
}

void BsonDocument::Reinit() {
    bson_reinit(static_cast<bson_t*>(RawBson()));
}

bool BsonDocument::InitFromJson(const char* json, int64_t len, MongoError* error) {
    return bson_init_from_json(static_cast<bson_t*>(RawBson()), json, len,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
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
double BsonIter::AsDoubleCoerce() const {
    return bson_iter_as_double(static_cast<const bson_iter_t*>(RawIter()));
}

int32_t BsonIter::AsInt32() const {
    return bson_iter_int32(static_cast<const bson_iter_t*>(RawIter()));
}

int64_t BsonIter::AsInt64() const {
    return bson_iter_int64(static_cast<const bson_iter_t*>(RawIter()));
}
int64_t BsonIter::AsInt64Coerce() const {
    return bson_iter_as_int64(static_cast<const bson_iter_t*>(RawIter()));
}

const char* BsonIter::AsUtf8(uint32_t* length) const {
    return bson_iter_utf8(static_cast<const bson_iter_t*>(RawIter()), length);
}

bool BsonIter::AsBool() const {
    return bson_iter_bool(static_cast<const bson_iter_t*>(RawIter()));
}

bool BsonIter::AsBoolCoerce() const {
    return bson_iter_as_bool(static_cast<const bson_iter_t*>(RawIter()));
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
void BsonIter::AsArray(uint32_t* array_len, const uint8_t** array) const {
    bson_iter_array(static_cast<const bson_iter_t*>(RawIter()), array_len, array);
}

const char* BsonIter::AsCode(uint32_t* length) const {
    return bson_iter_code(static_cast<const bson_iter_t*>(RawIter()), length);
}

void BsonIter::AsCodeWithScope(uint32_t* code_length, const char** code, BsonDocument* scope) const {
    uint32_t scope_len;
    const uint8_t* scope_data;
    bson_iter_codewscope(static_cast<const bson_iter_t*>(RawIter()), code_length, &scope_len, &scope_data);
    *code = bson_iter_code(static_cast<const bson_iter_t*>(RawIter()), code_length);
    if (scope && scope_data) {
        bson_destroy(static_cast<bson_t*>(scope->RawBson()));
        bson_t tmp;
        bson_init_static(&tmp, scope_data, scope_len);
        bson_copy_to(&tmp, static_cast<bson_t*>(scope->RawBson()));
        bson_destroy(&tmp);
    }
}

void BsonIter::AsRegex(const char** regex, const char** options) const {
    *regex = bson_iter_regex(static_cast<const bson_iter_t*>(RawIter()), options);
}

const char* BsonIter::AsSymbol(uint32_t* length) const {
    return bson_iter_symbol(static_cast<const bson_iter_t*>(RawIter()), length);
}

void BsonIter::AsTimestamp(uint32_t* timestamp, uint32_t* increment) const {
    bson_iter_timestamp(static_cast<const bson_iter_t*>(RawIter()), timestamp, increment);
}

time_t BsonIter::AsTimeT() const {
    return bson_iter_time_t(static_cast<const bson_iter_t*>(RawIter()));
}

bool BsonIter::AsDecimal128(MongoDecimal128* dec) const {
    if (!dec) return false;
    return bson_iter_decimal128(static_cast<const bson_iter_t*>(RawIter()),
                                 reinterpret_cast<bson_decimal128_t*>(dec));
}

bool BsonIter::FindCase(const char* key) {
    return bson_iter_find_case(static_cast<bson_iter_t*>(RawIter()), key);
}

bool BsonIter::FindDescendant(const char* dotkey, BsonIter* descendant) {
    if (!descendant) return false;
    return bson_iter_find_descendant(static_cast<bson_iter_t*>(RawIter()), dotkey,
                                      static_cast<bson_iter_t*>(descendant->RawIter()));
}

bool BsonIter::FindWLen(const char* key, int keylen) {
    return bson_iter_find_w_len(static_cast<bson_iter_t*>(RawIter()), key, keylen);
}

bool BsonIter::InitFind(const BsonDocument& doc, const char* key) {
    return bson_iter_init_find(static_cast<bson_iter_t*>(RawIter()),
                               static_cast<const bson_t*>(doc.RawBson()), key);
}

bool BsonIter::InitFindCase(const BsonDocument& doc, const char* key) {
    return bson_iter_init_find_case(static_cast<bson_iter_t*>(RawIter()),
                                    static_cast<const bson_t*>(doc.RawBson()), key);
}
bool BsonIter::InitFindWLen(const BsonDocument& doc, const char* key, int keylen) {
    return bson_iter_init_find_w_len(static_cast<bson_iter_t*>(RawIter()),
                                      static_cast<const bson_t*>(doc.RawBson()), key, keylen);
}

const char* BsonIter::KeyUnsafe() const {
    return bson_iter_key_unsafe(static_cast<const bson_iter_t*>(RawIter()));
}

char* BsonIter::DupUtf8(uint32_t* length) const {
    return bson_iter_dup_utf8(static_cast<const bson_iter_t*>(RawIter()), length);
}

BsonIter BsonIter::Recurse() const {
    BsonIter child;
    bson_iter_recurse(static_cast<const bson_iter_t*>(RawIter()),
                      static_cast<bson_iter_t*>(child.RawIter()));
    return child;
}

bool BsonIter::InitFromData(const uint8_t* data, size_t length) {
    return bson_iter_init_from_data(static_cast<bson_iter_t*>(RawIter()), data, length);
}
bool BsonIter::InitFromDataAtOffset(const uint8_t* data, size_t length,
                                     uint32_t offset, uint32_t keylen) {
    return bson_iter_init_from_data_at_offset(static_cast<bson_iter_t*>(RawIter()),
                                               data, length, offset, keylen);
}

uint32_t BsonIter::KeyLen() const {
    return bson_iter_key_len(static_cast<const bson_iter_t*>(RawIter()));
}

int BsonIter::BinarySubtype() const {
    return static_cast<int>(bson_iter_binary_subtype(
        static_cast<const bson_iter_t*>(RawIter())));
}

bool BsonIter::BinaryEqual(const BsonIter& a, const BsonIter& b) {
    return bson_iter_binary_equal(
        static_cast<const bson_iter_t*>(a.RawIter()),
        static_cast<const bson_iter_t*>(b.RawIter()));
}

void BsonIter::AsTimeval(void* tv) const {
    bson_iter_timeval(static_cast<const bson_iter_t*>(RawIter()),
                      static_cast<struct timeval*>(tv));
}
void BsonIter::AsDBPointer(uint32_t* collection_len, const char** collection,
                            const void** oid) const {
    bson_iter_dbpointer(static_cast<const bson_iter_t*>(RawIter()),
                         collection_len, collection,
                         reinterpret_cast<const bson_oid_t**>(oid));
}

bool BsonIter::VisitAll(const void* visitor, void* data) {
    return bson_iter_visit_all(static_cast<bson_iter_t*>(RawIter()),
                                static_cast<const bson_visitor_t*>(visitor), data);
}

uint32_t BsonIter::Offset() const {
    return bson_iter_offset(static_cast<bson_iter_t*>(const_cast<BsonIter*>(this)->RawIter()));
}

const void* BsonIter::Value() const {
    return bson_iter_value(static_cast<bson_iter_t*>(const_cast<BsonIter*>(this)->RawIter()));
}

void BsonIter::OverwriteInt32(int32_t value) {
    bson_iter_overwrite_int32(static_cast<bson_iter_t*>(RawIter()), value);
}

void BsonIter::OverwriteInt64(int64_t value) {
    bson_iter_overwrite_int64(static_cast<bson_iter_t*>(RawIter()), value);
}

void BsonIter::OverwriteDouble(double value) {
    bson_iter_overwrite_double(static_cast<bson_iter_t*>(RawIter()), value);
}

void BsonIter::OverwriteDecimal128(const MongoDecimal128& value) {
    bson_iter_overwrite_decimal128(static_cast<bson_iter_t*>(RawIter()),
        reinterpret_cast<const bson_decimal128_t*>(&value));
}

void BsonIter::OverwriteBool(bool value) {
    bson_iter_overwrite_bool(static_cast<bson_iter_t*>(RawIter()), value);
}

void BsonIter::OverwriteOid(const MongoOid& value) {
    bson_iter_overwrite_oid(static_cast<bson_iter_t*>(RawIter()),
        reinterpret_cast<const bson_oid_t*>(value.data()));
}

void BsonIter::OverwriteTimestamp(uint32_t timestamp, uint32_t increment) {
    bson_iter_overwrite_timestamp(static_cast<bson_iter_t*>(RawIter()),
                                   timestamp, increment);
}

void BsonIter::OverwriteDateTime(int64_t value) {
    bson_iter_overwrite_date_time(static_cast<bson_iter_t*>(RawIter()), value);
}

void BsonIter::OverwriteBinary(int subtype, uint32_t* binary_len, uint8_t** binary) {
    bson_iter_overwrite_binary(static_cast<bson_iter_t*>(RawIter()),
        static_cast<bson_subtype_t>(subtype), binary_len, binary);
}

void* BsonIter::RawIter() { return static_cast<void*>(storage_); }

const void* BsonIter::RawIter() const { return static_cast<const void*>(storage_); }

// ═══════════════════════════════════════════════════════════════════════
// MongoDecimal128
// ═══════════════════════════════════════════════════════════════════════

bool MongoDecimal128::FromString(const char* str) {
    return bson_decimal128_from_string(str,
        reinterpret_cast<bson_decimal128_t*>(this));
}
bool MongoDecimal128::FromStringLen(const char* str, int len) {
    return bson_decimal128_from_string_w_len(str, len,
        reinterpret_cast<bson_decimal128_t*>(this));
}

std::string MongoDecimal128::ToString() const {
    char buf[BSON_DECIMAL128_STRING];
    bson_decimal128_to_string(reinterpret_cast<const bson_decimal128_t*>(this), buf);
    return std::string(buf);
}

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

bool BsonArrayBuilder::AppendNull() {
    return bson_array_builder_append_null(
        static_cast<bson_array_builder_t*>(ptr_));
}

bool BsonArrayBuilder::AppendDateTime(int64_t msec_since_epoch) {
    return bson_array_builder_append_date_time(
        static_cast<bson_array_builder_t*>(ptr_), msec_since_epoch);
}

bool BsonArrayBuilder::AppendTimestamp(uint32_t timestamp, uint32_t increment) {
    return bson_array_builder_append_timestamp(
        static_cast<bson_array_builder_t*>(ptr_), timestamp, increment);
}

bool BsonArrayBuilder::AppendDocument(const BsonDocument& doc) {
    return bson_array_builder_append_document(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<const bson_t*>(doc.RawBson()));
}

bool BsonArrayBuilder::AppendArray(const BsonDocument& array) {
    return bson_array_builder_append_array(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<const bson_t*>(array.RawBson()));
}

bool BsonArrayBuilder::AppendBinary(int subtype, const uint8_t* data, uint32_t length) {
    return bson_array_builder_append_binary(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<bson_subtype_t>(subtype), data, length);
}

bool BsonArrayBuilder::AppendRegex(const char* regex, const char* options) {
    return bson_array_builder_append_regex_w_len(
        static_cast<bson_array_builder_t*>(ptr_), regex, -1, options);
}

bool BsonArrayBuilder::AppendCode(const char* javascript) {
    return bson_array_builder_append_code(
        static_cast<bson_array_builder_t*>(ptr_), javascript);
}

bool BsonArrayBuilder::AppendCodeWithScope(const char* javascript, const BsonDocument& scope) {
    return bson_array_builder_append_code_with_scope(
        static_cast<bson_array_builder_t*>(ptr_), javascript,
        static_cast<const bson_t*>(scope.RawBson()));
}

bool BsonArrayBuilder::AppendIter(const BsonIter& iter) {
    return bson_array_builder_append_iter(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<const bson_iter_t*>(iter.RawIter()));
}

bool BsonArrayBuilder::AppendValue(const void* bson_value) {
    return bson_array_builder_append_value(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<const bson_value_t*>(bson_value));
}

bool BsonArrayBuilder::AppendMinkey() {
    return bson_array_builder_append_minkey(static_cast<bson_array_builder_t*>(ptr_));
}

bool BsonArrayBuilder::AppendMaxkey() {
    return bson_array_builder_append_maxkey(static_cast<bson_array_builder_t*>(ptr_));
}

bool BsonArrayBuilder::AppendUndefined() {
    return bson_array_builder_append_undefined(static_cast<bson_array_builder_t*>(ptr_));
}

bool BsonArrayBuilder::AppendSymbol(const char* value) {
    return bson_array_builder_append_symbol(
        static_cast<bson_array_builder_t*>(ptr_), value, -1);
}

bool BsonArrayBuilder::AppendDBPointer(const char* collection, const MongoOid& oid) {
    return bson_array_builder_append_dbpointer(
        static_cast<bson_array_builder_t*>(ptr_), collection,
        reinterpret_cast<const bson_oid_t*>(oid.data()));
}

bool BsonArrayBuilder::AppendTimeT(time_t value) {
    return bson_array_builder_append_time_t(static_cast<bson_array_builder_t*>(ptr_), value);
}

bool BsonArrayBuilder::AppendTimeval(const void* tv) {
    return bson_array_builder_append_timeval(static_cast<bson_array_builder_t*>(ptr_),
                                              const_cast<struct timeval*>(static_cast<const struct timeval*>(tv)));
}

bool BsonArrayBuilder::AppendNowUtc() {
    return bson_array_builder_append_now_utc(static_cast<bson_array_builder_t*>(ptr_));
}

bool BsonArrayBuilder::AppendDecimal128(const MongoDecimal128& value) {
    return bson_array_builder_append_decimal128(
        static_cast<bson_array_builder_t*>(ptr_),
        reinterpret_cast<const bson_decimal128_t*>(&value));
}

bool BsonArrayBuilder::AppendArrayFromVector(const BsonIter& iter) {
    return bson_array_builder_append_array_from_vector(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<const bson_iter_t*>(iter.RawIter()));
}

bool BsonArrayBuilder::AppendBinaryUninit(int subtype, uint8_t** data_out, uint32_t len) {
    return bson_array_builder_append_binary_uninit(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<bson_subtype_t>(subtype), data_out, len);
}

bool BsonArrayBuilder::AppendDocumentBegin(BsonDocument* subdoc) {
    return bson_array_builder_append_document_begin(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<bson_t*>(subdoc->RawBson()));
}

bool BsonArrayBuilder::AppendDocumentEnd(BsonArrayBuilder* builder, BsonDocument* subdoc) {
    return bson_array_builder_append_document_end(
        static_cast<bson_array_builder_t*>(builder->ptr_),
        static_cast<bson_t*>(subdoc->RawBson()));
}

bool BsonArrayBuilder::AppendArrayBuilderBegin(void** builder_out) {
    return bson_array_builder_append_array_builder_begin(
        static_cast<bson_array_builder_t*>(ptr_),
        reinterpret_cast<bson_array_builder_t**>(builder_out));
}

bool BsonArrayBuilder::AppendArrayBuilderEnd(BsonArrayBuilder* builder, void* child) {
    return bson_array_builder_append_array_builder_end(
        static_cast<bson_array_builder_t*>(builder->ptr_),
        static_cast<bson_array_builder_t*>(child));
}

bool BsonArrayBuilder::Build(BsonDocument* out) {
    return bson_array_builder_build(
        static_cast<bson_array_builder_t*>(ptr_),
        static_cast<bson_t*>(out->RawBson()));
}

void* BsonArrayBuilder::Raw() { return ptr_; }

} // namespace mongo
} // namespace engine
