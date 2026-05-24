#pragma once

#include <quill/LogMacros.h>

#include "runtime/core/log/log_context.h"

// Plain logging macros
#define ENGINE_LOG_TRACE(logger, fmt, ...)   LOG_TRACE_L1(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_DEBUG(logger, fmt, ...)   LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_INFO(logger, fmt, ...)    LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_WARN(logger, fmt, ...)    LOG_WARNING(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_ERROR(logger, fmt, ...)   LOG_ERROR(logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_CRITICAL(logger, fmt, ...) LOG_CRITICAL(logger, fmt, ##__VA_ARGS__)

// Rate-limited variants
#define ENGINE_LOG_TRACE_LIMIT(d, logger, fmt, ...) \
    LOG_TRACE_L1_LIMIT(d, logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_DEBUG_LIMIT(d, logger, fmt, ...) \
    LOG_DEBUG_LIMIT(d, logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_INFO_LIMIT(d, logger, fmt, ...) \
    LOG_INFO_LIMIT(d, logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_WARN_LIMIT(d, logger, fmt, ...) \
    LOG_WARNING_LIMIT(d, logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_ERROR_LIMIT(d, logger, fmt, ...) \
    LOG_ERROR_LIMIT(d, logger, fmt, ##__VA_ARGS__)
#define ENGINE_LOG_CRITICAL_LIMIT(d, logger, fmt, ...) \
    LOG_CRITICAL_LIMIT(d, logger, fmt, ##__VA_ARGS__)

// Logging with trace context injected into message
#define ENGINE_LOG_CTX_TRACE(logger, fmt, ...)                                   \
    LOG_TRACE_L1(logger, "[{}][{}][{}] " fmt,                                    \
                 ::engine::TraceContext::trace_id(),                              \
                 ::engine::TraceContext::player_id(),                             \
                 ::engine::TraceContext::room_id(),                               \
                 ##__VA_ARGS__)

#define ENGINE_LOG_CTX_DEBUG(logger, fmt, ...)                                   \
    LOG_DEBUG(logger, "[{}][{}][{}] " fmt,                                       \
              ::engine::TraceContext::trace_id(),                                 \
              ::engine::TraceContext::player_id(),                                \
              ::engine::TraceContext::room_id(),                                  \
              ##__VA_ARGS__)

#define ENGINE_LOG_CTX_INFO(logger, fmt, ...)                                    \
    LOG_INFO(logger, "[{}][{}][{}] " fmt,                                        \
             ::engine::TraceContext::trace_id(),                                  \
             ::engine::TraceContext::player_id(),                                 \
             ::engine::TraceContext::room_id(),                                   \
             ##__VA_ARGS__)

#define ENGINE_LOG_CTX_WARN(logger, fmt, ...)                                    \
    LOG_WARNING(logger, "[{}][{}][{}] " fmt,                                     \
                ::engine::TraceContext::trace_id(),                               \
                ::engine::TraceContext::player_id(),                              \
                ::engine::TraceContext::room_id(),                                \
                ##__VA_ARGS__)

#define ENGINE_LOG_CTX_ERROR(logger, fmt, ...)                                   \
    LOG_ERROR(logger, "[{}][{}][{}] " fmt,                                       \
              ::engine::TraceContext::trace_id(),                                 \
              ::engine::TraceContext::player_id(),                                \
              ::engine::TraceContext::room_id(),                                  \
              ##__VA_ARGS__)

#define ENGINE_LOG_CTX_CRITICAL(logger, fmt, ...)                                 \
    LOG_CRITICAL(logger, "[{}][{}][{}] " fmt,                                    \
                 ::engine::TraceContext::trace_id(),                              \
                 ::engine::TraceContext::player_id(),                             \
                 ::engine::TraceContext::room_id(),                               \
                 ##__VA_ARGS__)
