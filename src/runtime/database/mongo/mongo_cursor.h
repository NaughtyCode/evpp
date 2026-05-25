#pragma once

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
    void SetBatchSize(uint32_t batch_size);

    // Internal: set from collection find. Not for public use.
    void SetCursor(void* cursor); // mongoc_cursor_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mongo
} // namespace engine
