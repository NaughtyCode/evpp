#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc-host-list.h — a singly-linked list of host:port entries.
// Used by server descriptions and topology introspection.
class ENGINE_API MongoHostList {
	public:
	MongoHostList();
	~MongoHostList();

	MongoHostList(const MongoHostList&) = delete;
	MongoHostList& operator=(const MongoHostList&) = delete;
	MongoHostList(MongoHostList&&) noexcept;
	MongoHostList& operator=(MongoHostList&&) noexcept;

	const char* GetHost() const;
	const char* GetHostAndPort() const;
	uint16_t GetPort() const;
	int GetFamily() const;

	MongoHostList* GetNext() const;

	void* Raw();  // returns mongoc_host_list_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
