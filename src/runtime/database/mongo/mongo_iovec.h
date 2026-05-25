#pragma once

#include <cstddef>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Mirrors mongoc_iovec_t (compatible with WSABUF on Windows, struct iovec on POSIX)
struct ENGINE_API MongoIovec {
    size_t iov_len;
    char*  iov_base;
};

} // namespace mongo
} // namespace engine
