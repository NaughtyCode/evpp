#pragma once

#include <cstdint>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Value type wrapping bson_error_t (504 bytes).
// Pass by pointer to methods that can fail; nullptr means "ignore error".
class ENGINE_API MongoError {
public:
    MongoError();
    ~MongoError();

    // Non-copyable but movable (bson_error_t has fixed-size buffer).
    MongoError(const MongoError&) = delete;
    MongoError& operator=(const MongoError&) = delete;
    MongoError(MongoError&&) noexcept;
    MongoError& operator=(MongoError&&) noexcept;

    void Clear();

    uint32_t Domain() const;
    uint32_t Code() const;
    const char* Message() const;

    // Access the raw bson_error_t pointer for internal use only.
    // Never expose this outside database/mongo/.
    void* RawError();             // returns bson_error_t*
    const void* RawError() const;

private:
    // Inline storage matching sizeof(bson_error_t) = 512 bytes.
    // Offsets verified at compile time in the .cc file.
    alignas(8) char storage_[512];
};

} // namespace mongo
} // namespace engine
