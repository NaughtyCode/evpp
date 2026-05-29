#pragma once

#include <cstdint>

#include "runtime/config/config_constants.h"

namespace engine {

/* Resource limits for DoS prevention — runtime-configurable.
 *
 * These defaults are conservative for a production game server.
 * Override via server.json → resource_limits section. */
struct ResourceLimits {
    /* Maximum size of a single network message on send (bytes). */
    uint32_t max_message_size     = config::kDefaultMaxMessageSize;

    /* Maximum total Buffer capacity per connection (bytes).
     * When reached, the connection stops reading new data. */
    uint32_t max_buffer_capacity  = config::kDefaultMaxBufferCapacity;

    /* Maximum HTTP POST body size (bytes). */
    uint32_t max_http_body_size   = config::kDefaultMaxHttpBodySize;

    /* Maximum msgpack nesting depth for encode operations. */
    uint32_t max_msgpack_depth    = config::kDefaultMaxMsgpackDepth;
};

}  // namespace engine
