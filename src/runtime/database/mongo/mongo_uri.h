#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>
#include <string>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_uri_t for parsing and constructing MongoDB connection URIs.
//
// Usage:
//   auto uri = MongoUri::New("mongodb://localhost:27017");
//   const char* db = uri.GetDatabase();
//   uri.SetAppname("MyApp");
class ENGINE_API MongoUri {
	public:
	static MongoUri New(const char* uri_string);
	static MongoUri NewWithError(const char* uri_string, MongoError* error);
	static MongoUri NewForHostPort(const char* hostname, uint16_t port);

	static char* Unescape(
		const char* escaped_string);  // Caller must bson_free() the returned string

	MongoUri();
	~MongoUri();

	MongoUri(const MongoUri&) = delete;
	MongoUri& operator=(const MongoUri&) = delete;
	MongoUri(MongoUri&& other) noexcept;
	MongoUri& operator=(MongoUri&& other) noexcept;

	MongoUri Copy() const;

	// Accessors
	const char* GetString() const;
	const char* GetDatabase() const;
	const char* GetUsername() const;
	const char* GetPassword() const;
	const char* GetAuthSource() const;
	const char* GetAuthMechanism() const;
	const char* GetReplicaSet() const;
	const char* GetAppname() const;
	const char* GetSrvHostname() const;
	const char* GetSrvServiceName() const;
	const void* GetHosts() const;
	const void* GetOptions() const;
	const void* GetCompressors() const;
	const void* GetCredentials() const;
	bool GetTls() const;
	bool HasOption(const char* key) const;
	bool GetMechanismProperties(BsonDocument& properties) const;
	const void* GetReadPrefs() const;
	const void* GetWriteConcern() const;
	const void* GetReadConcern() const;
	const char* GetServerMonitoringMode() const;

	// Setters
	bool SetDatabase(const char* database);
	bool SetUsername(const char* username);
	bool SetPassword(const char* password);
	bool SetAuthSource(const char* value);
	bool SetAuthMechanism(const char* value);
	bool SetAppname(const char* appname);
	bool SetCompressors(const char* compressors);
	bool SetMechanismProperties(const BsonDocument& properties);
	bool SetServerMonitoringMode(const char* value);
	void SetReadPrefs(const MongoReadPrefs& read_prefs);
	void SetWriteConcern(const MongoWriteConcern& write_concern);
	void SetReadConcern(const MongoReadConcern& read_concern);

	// Option value access (key type checks — static, operate on option names)
	static bool OptionIsInt32(const char* key);
	static bool OptionIsInt64(const char* key);
	static bool OptionIsBool(const char* key);
	static bool OptionIsUtf8(const char* key);

	// Generic option getters/setters
	int32_t GetOptionAsInt32(const char* option, int32_t fallback) const;
	int64_t GetOptionAsInt64(const char* option, int64_t fallback) const;
	bool GetOptionAsBool(const char* option, bool fallback) const;
	const char* GetOptionAsUtf8(const char* option, const char* fallback) const;
	bool SetOptionAsInt32(const char* option, int32_t value);
	bool SetOptionAsInt64(const char* option, int64_t value);
	bool SetOptionAsBool(const char* option, bool value);
	bool SetOptionAsUtf8(const char* option, const char* value);

	// Internal access
	void* RawUri();	 // returns mongoc_uri_t*
	const void* RawUri() const;
	void SetRawUri(void* uri);	// takes ownership of mongoc_uri_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
