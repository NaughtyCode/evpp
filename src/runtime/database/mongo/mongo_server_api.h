#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoServerApi {
	public:
	enum Version {
		kV1 = 0
	};

	// Create a new server API descriptor for the given version.
	static MongoServerApi New(Version version);

	// Returns a human-readable string for the version, e.g. "1".
	static const char* VersionToString(Version version);
	static bool VersionFromString(const char* str, Version* out);

	MongoServerApi();
	~MongoServerApi();

	MongoServerApi(const MongoServerApi&) = delete;
	MongoServerApi& operator=(const MongoServerApi&) = delete;
	MongoServerApi(MongoServerApi&& other) noexcept;
	MongoServerApi& operator=(MongoServerApi&& other) noexcept;

	MongoServerApi Copy() const;

	// Setters — configure stable API behaviour.
	void SetStrict(bool strict);
	void SetDeprecationErrors(bool deprecation_errors);

	// Getters — return false when the optional is not set.
	bool GetStrict() const;
	bool GetDeprecationErrors() const;
	Version GetVersion() const;

	// Internal access
	void* RawServerApi();
	const void* RawServerApi() const;

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
