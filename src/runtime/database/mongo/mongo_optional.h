#pragma once

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-optional.h — a simple optional bool container.
class ENGINE_API MongoOptional {
public:
    MongoOptional();
    ~MongoOptional() = default;

    MongoOptional(const MongoOptional& other);
    MongoOptional& operator=(const MongoOptional& other);

    void Init();
    bool IsSet() const;
    bool Value() const;
    void SetValue(bool val);
    void Copy(const MongoOptional& source);

    void* Raw(); // returns mongoc_optional_t*

private:
    // Inline storage matching sizeof(mongoc_optional_t) = sizeof(bool)+sizeof(bool)+padding
    alignas(8) char storage_[16];
};

} // namespace mongo
} // namespace engine
