#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


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
class CLOUD_ENGINE_API BsonContext {
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

	void* Raw();  // returns bson_context_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// String builder backed by std::string (bson_string_t was removed in libbson 2.x).
class CLOUD_ENGINE_API BsonString {
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

	void* Raw();

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
class CLOUD_ENGINE_API BsonJsonReader {
	public:
	// Create from a file descriptor, file, or memory buffer.
	static BsonJsonReader NewFromFd(int fd, bool close_on_destroy);
	static BsonJsonReader NewFromFile(const char* filename, MongoError* error);
	static BsonJsonReader NewFromData(const uint8_t* data, size_t length);
	// Callback-based reader: cb and dcb are bson_json_reader_cb / bson_json_destroy_cb function pointers.
	static BsonJsonReader New(
		void* data, void* cb, void* dcb, bool allow_multiple, size_t buf_size);

	BsonJsonReader();
	~BsonJsonReader();

	BsonJsonReader(const BsonJsonReader&) = delete;
	BsonJsonReader& operator=(const BsonJsonReader&) = delete;
	BsonJsonReader(BsonJsonReader&&) noexcept;
	BsonJsonReader& operator=(BsonJsonReader&&) noexcept;

	void Destroy();

	bool Read(BsonDocument* out, MongoError* error = nullptr);
	const char* ErrorDescription() const;

	void* Raw();  // returns bson_json_reader_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Incremental JSON data reader — feeds raw JSON bytes and produces BSON.
class CLOUD_ENGINE_API BsonJsonDataReader {
	public:
	BsonJsonDataReader();
	~BsonJsonDataReader();

	BsonJsonDataReader(const BsonJsonDataReader&) = delete;
	BsonJsonDataReader& operator=(const BsonJsonDataReader&) = delete;
	BsonJsonDataReader(BsonJsonDataReader&&) noexcept;
	BsonJsonDataReader& operator=(BsonJsonDataReader&&) noexcept;

	bool Ingest(const uint8_t* data, size_t len);
	void Destroy();

	void* Raw();  // returns bson_json_data_reader_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Incremental BSON binary reader (reads bson_t from a memory buffer).
class CLOUD_ENGINE_API BsonReader {
	public:
	static BsonReader NewFromData(const uint8_t* data, size_t length);
	static BsonReader NewFromFile(const char* path, MongoError* error);
	static BsonReader NewFromFd(int fd, bool close_on_destroy);
	static BsonReader NewFromHandle(void* handle, void* read_func, void* destroy_func);

	BsonReader();
	~BsonReader();

	BsonReader(const BsonReader&) = delete;
	BsonReader& operator=(const BsonReader&) = delete;
	BsonReader(BsonReader&&) noexcept;
	BsonReader& operator=(BsonReader&&) noexcept;

	void Destroy();

	const void* Read(bool* reached_eof) const;	// returns const bson_t*
	bool Read(BsonDocument* out, MongoError* error = nullptr);
	void SetData(const uint8_t* data, size_t length);
	void SetReadFunc(void* func);
	void SetDestroyFunc(void* func);
	int64_t Tell() const;
	void Reset();

	void* Raw();  // returns bson_reader_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Incremental BSON binary writer.
class CLOUD_ENGINE_API BsonWriter {
	public:
	BsonWriter();
	~BsonWriter();

	BsonWriter(const BsonWriter&) = delete;
	BsonWriter& operator=(const BsonWriter&) = delete;
	BsonWriter(BsonWriter&&) noexcept;
	BsonWriter& operator=(BsonWriter&&) noexcept;

	void Destroy();

	// Begin a new document or array.
	bool Begin(const void* raw_bson = nullptr);	 // bson_t* or nullptr for empty
	bool BeginDocument(BsonDocument* doc);
	bool BeginArray(BsonDocument* array);

	// End the current document/array — finalizes it into `out`.
	void End(BsonDocument* out);

	bool Rollback();

	// Get the current buffer (not yet finalized).
	const uint8_t* GetBuffer(size_t* length) const;
	size_t GetLength() const;

	void* Raw();  // returns bson_writer_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Clock utilities (wraps bson-clock.h).
class CLOUD_ENGINE_API BsonClock {
	public:
	static int64_t GetTimeNs();
	static int64_t GetDateTime();
	static void GetTimeOfDay(void* tv);	 // struct timeval*
};

// UTF-8 validation utilities (wraps bson-utf8.h).
class CLOUD_ENGINE_API BsonUtf8 {
	public:
	static bool Validate(const char* str, size_t length, bool allow_null = false);
	static char* EscapeForJson(const char* str, size_t length);
	static uint32_t GetChar(const char* utf8);
	static const char* NextChar(const char* utf8);
	static void FromUnichar(uint32_t unichar, char utf8[6], uint32_t* len);
};

// JSON serialization options (wraps bson_json_opts_t).
class CLOUD_ENGINE_API BsonJsonOpts {
	public:
	BsonJsonOpts(BsonJsonMode mode = BsonJsonMode::kLegacy, int32_t max_len = -1);
	~BsonJsonOpts();

	BsonJsonOpts(const BsonJsonOpts&) = delete;
	BsonJsonOpts& operator=(const BsonJsonOpts&) = delete;
	BsonJsonOpts(BsonJsonOpts&&) noexcept;
	BsonJsonOpts& operator=(BsonJsonOpts&&) noexcept;

	void SetOutermostArray(bool is_outermost_array);

	void* Raw();  // returns bson_json_opts_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// RAII wrapper around bson_value_t for value copy/destroy lifecycle.
class CLOUD_ENGINE_API BsonValue {
	public:
	BsonValue();
	~BsonValue();

	BsonValue(const BsonValue& other);
	BsonValue& operator=(const BsonValue& other);
	BsonValue(BsonValue&&) noexcept;
	BsonValue& operator=(BsonValue&&) noexcept;

	void Copy(const BsonValue& src);
	void Destroy();

	void* Raw();  // returns bson_value_t*
	const void* Raw() const;

	private:
	// bson_value_t is a tagged union; inline storage
	alignas(8) char storage_[64];
};

// String utility functions (wraps bson-string.h free functions).
class CLOUD_ENGINE_API BsonStrUtil {
	public:
	static char* Strdup(const char* str);
	static char* Strndup(const char* str, size_t n_bytes);
	static char* StrdupPrintf(const char* format, ...);
	static char* StrdupvPrintf(const char* format, va_list args);
	static void Strncpy(char* dst, const char* src, size_t size);
	static void Vsnprintf(char* str, size_t size, const char* format, va_list ap);
	static void Snprintf(char* str, size_t size, const char* format, ...);
	static void Strfreev(char** strv);
	static size_t Strnlen(const char* s, size_t maxlen);
	static int64_t AsciiStrtoll(const char* str, char** endptr, int base);
	static int Strcasecmp(const char* s1, const char* s2);
	static bool Isspace(int c);
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

}  // namespace mongo
}  // namespace engine

#endif
