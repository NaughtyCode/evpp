#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoCursor {
	public:
	MongoCursor();
	~MongoCursor();

	MongoCursor(const MongoCursor&) = delete;
	MongoCursor& operator=(const MongoCursor&) = delete;
	MongoCursor(MongoCursor&& other) noexcept;
	MongoCursor& operator=(MongoCursor&& other) noexcept;

	void Destroy();

	bool Next(BsonDocument* out);
	bool HasError(MongoError* error) const;

	// ── Cursor control ────────────────────────────────────────────────
	const void* Current() const;  // returns const bson_t* (current doc without advancing)
	bool More();  // is there another doc?
	bool ErrorDocument(MongoError* error, const void** doc) const;
	MongoCursor* Clone() const;	 // clone cursor (not fully supported in 2.x — prefer re-query)

	// ── Batch / limit ──────────────────────────────────────────────────
	void SetBatchSize(uint32_t batch_size);
	uint32_t GetBatchSize() const;
	void SetLimit(int64_t limit);
	int64_t GetLimit() const;

	// ── Server / metadata ──────────────────────────────────────────────
	int64_t GetId() const;
	uint32_t GetServerId() const;
	void SetServerId(uint32_t server_id);
	void GetHost(void* host_out) const;
	void SetMaxAwaitTimeMs(uint32_t max_await_ms);
	uint32_t GetMaxAwaitTimeMs() const;

	// Factory: create a cursor from a command reply.
	static MongoCursor* NewFromCommandReplyWithOpts(void* client,
													const BsonDocument& reply,
													const BsonDocument* opts);

	// Internal: set from collection find. Not for public use.
	void SetCursor(void* cursor);  // mongoc_cursor_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
