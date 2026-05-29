#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// 12-byte MongoDB ObjectId. Binary-compatible with bson_oid_t.
class CLOUD_ENGINE_API MongoOid {
	public:
	MongoOid();
	~MongoOid() = default;

	void Init();
	void InitFromString(const char* str);
	void InitFromData(const uint8_t* data);

	std::string ToString() const;

	int Compare(const MongoOid& other) const;
	bool Equal(const MongoOid& other) const;
	bool IsValid(const char* str, size_t length) const;
	uint32_t Hash() const;

	void SetBytes(const uint8_t bytes[12]);
	const uint8_t* GetBytes() const;

	void Copy(const MongoOid& src);
	time_t GetTimeT() const;

	// Internal: direct access to the 12-byte buffer, reinterpret_cast to bson_oid_t.
	const uint8_t* data() const {
		return bytes_;
	}
	uint8_t* data() {
		return bytes_;
	}

	private:
	uint8_t bytes_[12];
};

static_assert(sizeof(MongoOid) == 12, "MongoOid must be 12 bytes");

}  // namespace mongo
}  // namespace engine

#endif
