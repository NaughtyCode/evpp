#pragma once

#include <cstdint>

namespace engine {

/* Resource limits for DoS prevention — configurable per subsystem.
 *
 * These defaults are conservative for a production game server.
 * Raise them via config if your workload requires larger payloads. */
struct ResourceLimits {
    /* Maximum size of a single network message on send (bytes).
     * Matches LengthPrefixedCodec default for TCP; also applies to UDP/KCP. */
    static constexpr uint32_t kDefaultMaxMessageSize = 64 * 1024;       /* 64 KiB */

    /* Maximum total Buffer capacity per connection (bytes).
     * When reached, the connection stops reading new data. */
    static constexpr uint32_t kDefaultMaxBufferCapacity = 256 * 1024;   /* 256 KiB */

    /* Maximum HTTP POST body size (bytes). */
    static constexpr uint32_t kDefaultMaxHttpBodySize = 10 * 1024 * 1024; /* 10 MiB */

    /* Maximum msgpack nesting depth for encode operations. */
    static constexpr uint32_t kDefaultMaxMsgpackDepth = 64;
};

}  // namespace engine
