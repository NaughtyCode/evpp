#pragma once

#include <quill/LogMacros.h>

#include "runtime/core/log/log_context.h"

// Plain logging macros.
// GetLogger() returns nullptr before InitLogger() and after ShutdownLogger().
// Keep engine-level logging calls safe in tests, tools, and embedded hosts that
// intentionally run runtime code without starting the logging backend.
#define ENGINE_LOG_TRACE(logger, fmt, ...)                                      \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_TRACE_L1(engine_log_logger__, fmt, ##__VA_ARGS__);              \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_DEBUG(logger, fmt, ...)                                      \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_DEBUG(engine_log_logger__, fmt, ##__VA_ARGS__);                 \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_INFO(logger, fmt, ...)                                       \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_INFO(engine_log_logger__, fmt, ##__VA_ARGS__);                  \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_WARN(logger, fmt, ...)                                       \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_WARNING(engine_log_logger__, fmt, ##__VA_ARGS__);               \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_ERROR(logger, fmt, ...)                                      \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_ERROR(engine_log_logger__, fmt, ##__VA_ARGS__);                 \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_CRITICAL(logger, fmt, ...)                                   \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_CRITICAL(engine_log_logger__, fmt, ##__VA_ARGS__);              \
		}                                                                       \
	} while (false)

// Rate-limited variants
#define ENGINE_LOG_TRACE_LIMIT(d, logger, fmt, ...) \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_TRACE_L1_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);     \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_DEBUG_LIMIT(d, logger, fmt, ...)                             \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_DEBUG_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);        \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_INFO_LIMIT(d, logger, fmt, ...)                              \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_INFO_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);         \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_WARN_LIMIT(d, logger, fmt, ...)                              \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_WARNING_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);      \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_ERROR_LIMIT(d, logger, fmt, ...)                             \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_ERROR_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);        \
		}                                                                       \
	} while (false)
#define ENGINE_LOG_CRITICAL_LIMIT(d, logger, fmt, ...)                          \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_CRITICAL_LIMIT(d, engine_log_logger__, fmt, ##__VA_ARGS__);     \
		}                                                                       \
	} while (false)

// Logging with trace context injected into message
#define ENGINE_LOG_CTX_TRACE(logger, fmt, ...)                                  \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_TRACE_L1(engine_log_logger__,                                  \
						 "[{}][{}][{}] " fmt,                                 \
						 ::engine::TraceContext::trace_id(),                  \
						 ::engine::TraceContext::player_id(),                 \
						 ::engine::TraceContext::room_id(),                   \
						 ##__VA_ARGS__);                                       \
		}                                                                       \
	} while (false)

#define ENGINE_LOG_CTX_DEBUG(logger, fmt, ...)                                  \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_DEBUG(engine_log_logger__,                                     \
					  "[{}][{}][{}] " fmt,                                    \
					  ::engine::TraceContext::trace_id(),                     \
					  ::engine::TraceContext::player_id(),                    \
					  ::engine::TraceContext::room_id(),                      \
					  ##__VA_ARGS__);                                          \
		}                                                                       \
	} while (false)

#define ENGINE_LOG_CTX_INFO(logger, fmt, ...)                                   \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_INFO(engine_log_logger__,                                      \
					 "[{}][{}][{}] " fmt,                                     \
					 ::engine::TraceContext::trace_id(),                      \
					 ::engine::TraceContext::player_id(),                     \
					 ::engine::TraceContext::room_id(),                       \
					 ##__VA_ARGS__);                                           \
		}                                                                       \
	} while (false)

#define ENGINE_LOG_CTX_WARN(logger, fmt, ...)                                   \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_WARNING(engine_log_logger__,                                   \
						"[{}][{}][{}] " fmt,                                  \
						::engine::TraceContext::trace_id(),                   \
						::engine::TraceContext::player_id(),                  \
						::engine::TraceContext::room_id(),                    \
						##__VA_ARGS__);                                        \
		}                                                                       \
	} while (false)

#define ENGINE_LOG_CTX_ERROR(logger, fmt, ...)                                  \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_ERROR(engine_log_logger__,                                     \
					  "[{}][{}][{}] " fmt,                                    \
					  ::engine::TraceContext::trace_id(),                     \
					  ::engine::TraceContext::player_id(),                    \
					  ::engine::TraceContext::room_id(),                      \
					  ##__VA_ARGS__);                                          \
		}                                                                       \
	} while (false)

#define ENGINE_LOG_CTX_CRITICAL(logger, fmt, ...)                               \
	do {                                                                        \
		auto* const engine_log_logger__ = (logger);                             \
		if (engine_log_logger__) {                                              \
			LOG_CRITICAL(engine_log_logger__,                                  \
						 "[{}][{}][{}] " fmt,                                 \
						 ::engine::TraceContext::trace_id(),                  \
						 ::engine::TraceContext::player_id(),                 \
						 ::engine::TraceContext::room_id(),                   \
						 ##__VA_ARGS__);                                       \
		}                                                                       \
	} while (false)
