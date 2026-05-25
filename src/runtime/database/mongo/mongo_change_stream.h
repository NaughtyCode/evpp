#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

class ENGINE_API MongoChangeStream {
public:
    MongoChangeStream();
    ~MongoChangeStream();

    MongoChangeStream(const MongoChangeStream&) = delete;
    MongoChangeStream& operator=(const MongoChangeStream&) = delete;
    MongoChangeStream(MongoChangeStream&& other) noexcept;
    MongoChangeStream& operator=(MongoChangeStream&& other) noexcept;

    void Destroy();

    // Advance to the next document in the stream. Returns false when there
    // are no more documents (timeout or end-of-stream).
    bool Next(BsonDocument* out);

    // Retrieve the resume token after a successful Next().
    const BsonDocument* GetResumeToken() const;

    // Check for error on the stream.
    bool ErrorDocument(MongoError* error, const BsonDocument** doc) const;

    // Internal: set from collection/database/client watch. Not for public use.
    void SetRawStream(void* stream); // mongoc_change_stream_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
