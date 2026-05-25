#include "runtime/database/mongo/mongo_bson_ext.h"

#include <cstdarg>
#include <string>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// BsonContext
// ═══════════════════════════════════════════════════════════════════════

struct BsonContext::Impl {
    bson_context_t* ctx = nullptr;
    bool owned = true;
};

BsonContext BsonContext::New() {
    BsonContext ctx;
    ctx.impl_->ctx = bson_context_new(BSON_CONTEXT_NONE);
    // The default context doesn't need freeing; only use it
    return ctx;
}

const BsonContext& BsonContext::Default() {
    static BsonContext default_ctx;
    if (!default_ctx.impl_->ctx) {
        default_ctx.impl_->ctx = bson_context_get_default();
        default_ctx.impl_->owned = false;
    }
    return default_ctx;
}

BsonContext::BsonContext() : impl_(std::make_unique<Impl>()) {}

BsonContext::~BsonContext() {
    // Default context is shared; don't destroy
}

BsonContext::BsonContext(BsonContext&&) noexcept = default;
BsonContext& BsonContext::operator=(BsonContext&&) noexcept = default;

void* BsonContext::Raw() { return impl_ ? impl_->ctx : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonString
// ═══════════════════════════════════════════════════════════════════════

struct BsonString::Impl {
    std::string str;
};

BsonString::BsonString() : impl_(std::make_unique<Impl>()) {}

BsonString::BsonString(const char* str) : impl_(std::make_unique<Impl>()) {
    if (str) impl_->str = str;
}

BsonString::~BsonString() = default;

BsonString::BsonString(BsonString&&) noexcept = default;
BsonString& BsonString::operator=(BsonString&&) noexcept = default;

void BsonString::Append(const char* str) {
    if (impl_ && str) impl_->str += str;
}

void BsonString::AppendPrintf(const char* format, ...) {
    if (impl_ && format) {
        va_list args;
        va_start(args, format);
        // Use vsnprintf to determine needed buffer size
        va_list args_copy;
        va_copy(args_copy, args);
        int needed = vsnprintf(nullptr, 0, format, args_copy);
        va_end(args_copy);
        if (needed > 0) {
            std::string buf(static_cast<size_t>(needed), '\0');
            vsnprintf(&buf[0], static_cast<size_t>(needed) + 1, format, args);
            impl_->str += buf;
        }
        va_end(args);
    }
}

const char* BsonString::GetString() const {
    return impl_ ? impl_->str.c_str() : nullptr;
}

size_t BsonString::GetLength() const {
    return impl_ ? impl_->str.size() : 0;
}

bool BsonString::Empty() const {
    return GetLength() == 0;
}

void BsonString::Truncate(size_t len) {
    if (impl_) impl_->str.resize(len);
}

void* BsonString::Raw() { return impl_ ? &impl_->str : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonJsonReader
// ═══════════════════════════════════════════════════════════════════════

struct BsonJsonReader::Impl {
    bson_json_reader_t* reader = nullptr;
};

BsonJsonReader BsonJsonReader::NewFromFd(int fd, bool close_on_destroy) {
    BsonJsonReader r;
    r.impl_->reader = bson_json_reader_new_from_fd(fd, close_on_destroy);
    return r;
}

BsonJsonReader BsonJsonReader::NewFromFile(const char* filename, MongoError* error) {
    BsonJsonReader r;
    r.impl_->reader = bson_json_reader_new_from_file(filename,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    return r;
}

BsonJsonReader BsonJsonReader::NewFromData(const uint8_t* data, size_t length) {
    BsonJsonReader r;
    r.impl_->reader = bson_json_data_reader_new(false, 0);
    bson_json_data_reader_ingest(r.impl_->reader, data, length);
    return r;
}

BsonJsonReader BsonJsonReader::New(void* data, void* cb, void* dcb, bool allow_multiple, size_t buf_size) {
    BsonJsonReader r;
    r.impl_->reader = bson_json_reader_new(data,
        reinterpret_cast<bson_json_reader_cb>(cb),
        reinterpret_cast<bson_json_destroy_cb>(dcb),
        allow_multiple, buf_size);
    return r;
}

BsonJsonReader::BsonJsonReader() : impl_(std::make_unique<Impl>()) {}

BsonJsonReader::~BsonJsonReader() {
    Destroy();
}

BsonJsonReader::BsonJsonReader(BsonJsonReader&&) noexcept = default;
BsonJsonReader& BsonJsonReader::operator=(BsonJsonReader&&) noexcept = default;

void BsonJsonReader::Destroy() {
    if (impl_ && impl_->reader) {
        bson_json_reader_destroy(impl_->reader);
        impl_->reader = nullptr;
    }
}

bool BsonJsonReader::Read(BsonDocument* out, MongoError* error) {
    if (!impl_ || !impl_->reader || !out) return false;
    bson_error_t err;
    out->Reinit();
    bool ok = bson_json_reader_read(impl_->reader,
        static_cast<bson_t*>(out->RawBson()),
        error ? static_cast<bson_error_t*>(error->RawError()) : &err);
    return ok;
}

const char* BsonJsonReader::ErrorDescription() const {
    // bson_json_reader_t is opaque in libbson 2.x; the error description is
    // embedded in the bson_error_t returned by Read(), so return nullptr here.
    (void)impl_;
    return nullptr;
}

void* BsonJsonReader::Raw() { return impl_ ? impl_->reader : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonJsonDataReader
// ═══════════════════════════════════════════════════════════════════════

struct BsonJsonDataReader::Impl {
    bson_json_reader_t* reader = nullptr;
};

BsonJsonDataReader::BsonJsonDataReader() : impl_(std::make_unique<Impl>()) {
    impl_->reader = bson_json_data_reader_new(false, 0);
}

BsonJsonDataReader::~BsonJsonDataReader() {
    Destroy();
}

BsonJsonDataReader::BsonJsonDataReader(BsonJsonDataReader&&) noexcept = default;
BsonJsonDataReader& BsonJsonDataReader::operator=(BsonJsonDataReader&&) noexcept = default;

void BsonJsonDataReader::Destroy() {
    if (impl_ && impl_->reader) {
        bson_json_reader_destroy(impl_->reader);
        impl_->reader = nullptr;
    }
}

bool BsonJsonDataReader::Ingest(const uint8_t* data, size_t len) {
    if (!impl_ || !impl_->reader) return false;
    bson_json_data_reader_ingest(impl_->reader, data, len);
    return true;
}

void* BsonJsonDataReader::Raw() { return impl_ ? impl_->reader : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonReader
// ═══════════════════════════════════════════════════════════════════════

struct BsonReader::Impl {
    bson_reader_t* reader = nullptr;
};

BsonReader BsonReader::NewFromData(const uint8_t* data, size_t length) {
    BsonReader r;
    r.impl_->reader = bson_reader_new_from_data(data, length);
    return r;
}

BsonReader BsonReader::NewFromFile(const char* path, MongoError* error) {
    BsonReader r;
    r.impl_->reader = bson_reader_new_from_file(path,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    return r;
}

BsonReader BsonReader::NewFromFd(int fd, bool close_on_destroy) {
    BsonReader r;
    r.impl_->reader = bson_reader_new_from_fd(fd, close_on_destroy);
    return r;
}

BsonReader BsonReader::NewFromHandle(void* handle, void* read_func, void* destroy_func) {
    BsonReader r;
    r.impl_->reader = bson_reader_new_from_handle(handle,
        reinterpret_cast<bson_reader_read_func_t>(read_func),
        reinterpret_cast<bson_reader_destroy_func_t>(destroy_func));
    return r;
}

BsonReader::BsonReader() : impl_(std::make_unique<Impl>()) {}

BsonReader::~BsonReader() {
    Destroy();
}

BsonReader::BsonReader(BsonReader&&) noexcept = default;
BsonReader& BsonReader::operator=(BsonReader&&) noexcept = default;

void BsonReader::Destroy() {
    if (impl_ && impl_->reader) {
        bson_reader_destroy(impl_->reader);
        impl_->reader = nullptr;
    }
}

const void* BsonReader::Read(bool* reached_eof) const {
    if (!impl_ || !impl_->reader) return nullptr;
    return bson_reader_read(impl_->reader, reached_eof);
}

bool BsonReader::Read(BsonDocument* out, MongoError* error) {
    if (!impl_ || !impl_->reader || !out) return false;
    bool eof = false;
    const bson_t* raw = bson_reader_read(impl_->reader, &eof);
    if (!raw) return false;
    bson_copy_to_excluding_noinit(raw, static_cast<bson_t*>(out->RawBson()), "", nullptr);
    return true;
}

void BsonReader::SetData(const uint8_t* data, size_t length) {
    if (impl_) {
        bson_reader_destroy(impl_->reader);
        impl_->reader = bson_reader_new_from_data(data, length);
    }
}

void BsonReader::SetReadFunc(void* func) {
    if (impl_ && impl_->reader)
        bson_reader_set_read_func(impl_->reader,
            reinterpret_cast<bson_reader_read_func_t>(func));
}

void BsonReader::SetDestroyFunc(void* func) {
    if (impl_ && impl_->reader)
        bson_reader_set_destroy_func(impl_->reader,
            reinterpret_cast<bson_reader_destroy_func_t>(func));
}

int64_t BsonReader::Tell() const {
    return impl_ && impl_->reader ? static_cast<int64_t>(bson_reader_tell(impl_->reader)) : 0;
}

void BsonReader::Reset() {
    if (impl_ && impl_->reader)
        bson_reader_reset(impl_->reader);
}

void* BsonReader::Raw() { return impl_ ? impl_->reader : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonWriter
// ═══════════════════════════════════════════════════════════════════════

struct BsonWriter::Impl {
    bson_writer_t* writer = nullptr;
    uint8_t* buf = nullptr;
    size_t buflen = 0;
};

BsonWriter::BsonWriter() : impl_(std::make_unique<Impl>()) {
    impl_->writer = bson_writer_new(&impl_->buf, &impl_->buflen, 0, bson_realloc_ctx, nullptr);
}

BsonWriter::~BsonWriter() {
    Destroy();
}

BsonWriter::BsonWriter(BsonWriter&&) noexcept = default;
BsonWriter& BsonWriter::operator=(BsonWriter&&) noexcept = default;

void BsonWriter::Destroy() {
    if (impl_ && impl_->writer) {
        bson_writer_destroy(impl_->writer);
        impl_->writer = nullptr;
        bson_free(impl_->buf);
        impl_->buf = nullptr;
        impl_->buflen = 0;
    }
}

bool BsonWriter::Begin(const void* raw_bson) {
    if (!impl_ || !impl_->writer) return false;
    (void)raw_bson; // new API: writer always provides the bson_t*
    bson_t* doc = nullptr;
    return bson_writer_begin(impl_->writer, &doc);
}

bool BsonWriter::BeginDocument(BsonDocument* doc) {
    return Begin(static_cast<const bson_t*>(doc->RawBson()));
}

bool BsonWriter::BeginArray(BsonDocument* array) {
    return BeginDocument(array);
}

void BsonWriter::End(BsonDocument* out) {
    if (impl_ && impl_->writer) {
        (void)out;
        bson_writer_end(impl_->writer);
    }
}

bool BsonWriter::Rollback() {
    if (impl_ && impl_->writer) {
        bson_writer_rollback(impl_->writer);
        return true;
    }
    return false;
}

const uint8_t* BsonWriter::GetBuffer(size_t* length) const {
    if (!impl_ || !impl_->writer) return nullptr;
    size_t len = bson_writer_get_length(impl_->writer);
    if (length) *length = len;
    return impl_->buf;
}

size_t BsonWriter::GetLength() const {
    return impl_ && impl_->writer ? bson_writer_get_length(impl_->writer) : 0;
}

void* BsonWriter::Raw() { return impl_ ? impl_->writer : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonClock
// ═══════════════════════════════════════════════════════════════════════

int64_t BsonClock::GetTimeNs() {
    return bson_get_monotonic_time();
}

int64_t BsonClock::GetDateTime() {
    struct timeval tv;
    bson_gettimeofday(&tv);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

void BsonClock::GetTimeOfDay(void* tv) {
    bson_gettimeofday(static_cast<struct timeval*>(tv));
}

// ═══════════════════════════════════════════════════════════════════════
// BsonUtf8
// ═══════════════════════════════════════════════════════════════════════

bool BsonUtf8::Validate(const char* str, size_t length, bool allow_null) {
    return bson_utf8_validate(str, length, allow_null);
}

char* BsonUtf8::EscapeForJson(const char* str, size_t length) {
    return bson_utf8_escape_for_json(str, static_cast<ssize_t>(length));
}

uint32_t BsonUtf8::GetChar(const char* utf8) {
    return bson_utf8_get_char(utf8);
}

const char* BsonUtf8::NextChar(const char* utf8) {
    return bson_utf8_next_char(utf8);
}

void BsonUtf8::FromUnichar(uint32_t unichar, char utf8[6], uint32_t* len) {
    bson_utf8_from_unichar(unichar, utf8, len);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonJsonOpts
// ═══════════════════════════════════════════════════════════════════════

struct BsonJsonOpts::Impl {
    bson_json_opts_t* opts = nullptr;
};

BsonJsonOpts::BsonJsonOpts(BsonJsonMode mode, int32_t max_len)
    : impl_(std::make_unique<Impl>()) {
    impl_->opts = bson_json_opts_new(static_cast<bson_json_mode_t>(mode), max_len);
}

BsonJsonOpts::~BsonJsonOpts() {
    if (impl_ && impl_->opts) bson_json_opts_destroy(impl_->opts);
}

BsonJsonOpts::BsonJsonOpts(BsonJsonOpts&&) noexcept = default;
BsonJsonOpts& BsonJsonOpts::operator=(BsonJsonOpts&&) noexcept = default;

void BsonJsonOpts::SetOutermostArray(bool is_outermost_array) {
    if (impl_ && impl_->opts)
        bson_json_opts_set_outermost_array(impl_->opts, is_outermost_array);
}

void* BsonJsonOpts::Raw() { return impl_ ? impl_->opts : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// BsonValue
// ═══════════════════════════════════════════════════════════════════════

BsonValue::BsonValue() {
    memset(storage_, 0, sizeof(storage_));
}

BsonValue::~BsonValue() {
    Destroy();
}

BsonValue::BsonValue(const BsonValue& other) {
    memset(storage_, 0, sizeof(storage_));
    bson_value_copy(reinterpret_cast<const bson_value_t*>(other.storage_),
                    reinterpret_cast<bson_value_t*>(storage_));
}

BsonValue& BsonValue::operator=(const BsonValue& other) {
    if (this != &other) {
        Destroy();
        bson_value_copy(reinterpret_cast<const bson_value_t*>(other.storage_),
                        reinterpret_cast<bson_value_t*>(storage_));
    }
    return *this;
}

BsonValue::BsonValue(BsonValue&&) noexcept = default;
BsonValue& BsonValue::operator=(BsonValue&&) noexcept = default;

void BsonValue::Copy(const BsonValue& src) {
    Destroy();
    bson_value_copy(reinterpret_cast<const bson_value_t*>(src.storage_),
                    reinterpret_cast<bson_value_t*>(storage_));
}

void BsonValue::Destroy() {
    bson_value_destroy(reinterpret_cast<bson_value_t*>(storage_));
    memset(storage_, 0, sizeof(storage_));
}

void* BsonValue::Raw() { return storage_; }
const void* BsonValue::Raw() const { return storage_; }

// ═══════════════════════════════════════════════════════════════════════
// BsonStrUtil
// ═══════════════════════════════════════════════════════════════════════

char* BsonStrUtil::Strdup(const char* str) {
    return bson_strdup(str);
}

char* BsonStrUtil::Strndup(const char* str, size_t n_bytes) {
    return bson_strndup(str, n_bytes);
}

char* BsonStrUtil::StrdupPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char* result = bson_strdupv_printf(format, args);
    va_end(args);
    return result;
}

char* BsonStrUtil::StrdupvPrintf(const char* format, va_list args) {
    return bson_strdupv_printf(format, args);
}

void BsonStrUtil::Strncpy(char* dst, const char* src, size_t size) {
    bson_strncpy(dst, src, size);
}

void BsonStrUtil::Vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    bson_vsnprintf(str, size, format, ap);
}

void BsonStrUtil::Snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    bson_vsnprintf(str, size, format, args);
    va_end(args);
}

void BsonStrUtil::Strfreev(char** strv) {
    bson_strfreev(strv);
}

size_t BsonStrUtil::Strnlen(const char* s, size_t maxlen) {
    return bson_strnlen(s, maxlen);
}

int64_t BsonStrUtil::AsciiStrtoll(const char* str, char** endptr, int base) {
    return bson_ascii_strtoll(str, endptr, base);
}

int BsonStrUtil::Strcasecmp(const char* s1, const char* s2) {
    return bson_strcasecmp(s1, s2);
}

bool BsonStrUtil::Isspace(int c) {
    return bson_isspace(c);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonKeys constants
// ═══════════════════════════════════════════════════════════════════════

namespace BsonKeys {

size_t Uint32ToString(uint32_t value, const char** strptr, char* str, size_t size) {
    return bson_uint32_to_string(value, strptr, str, size);
}

const char* kOid = "$oid";
    const char* kSet = "$set";
    const char* kUnset = "$unset";
    const char* kInc = "$inc";
    const char* kPush = "$push";
    const char* kPull = "$pull";
    const char* kGte = "$gte";
    const char* kLte = "$lte";
    const char* kGt = "$gt";
    const char* kLt = "$lt";
    const char* kNe = "$ne";
    const char* kIn = "$in";
    const char* kNin = "$nin";
    const char* kExists = "$exists";
    const char* kRegex = "$regex";
    const char* kOptions = "$options";
    const char* kAnd = "$and";
    const char* kOr = "$or";
    const char* kNor = "$nor";
    const char* kNot = "$not";
    const char* kSize = "$size";
    const char* kType = "$type";
    const char* kAll = "$all";
    const char* kElemMatch = "$elemMatch";
    const char* kSlice = "$slice";
    const char* kSearch = "$search";
    const char* kLanguage = "$language";
    const char* kText = "$text";
    const char* kComment = "$comment";
    const char* kBits = "$bits";
    const char* kNearSphere = "$nearSphere";
    const char* kMaxDistance = "$maxDistance";
    const char* kMinDistance = "$minDistance";
    const char* kGeometry = "$geometry";
    const char* kUniqueDocs = "$uniqueDocs";
}

} // namespace mongo
} // namespace engine
