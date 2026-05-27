#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "CullingEngineAPI.h"

namespace CullingEngine {
/*
 * Severity level attached to a log entry.
 */
enum class Level : std::uint8_t { kTrace, kDebug, kInfo, kWarning, kError };

/*
 * Installs the callback used by the lightweight logging module.
 *
 * Parameters:
 *   callback - Function invoked for future log messages. Passing nullptr
 *              clears the callback.
 */
CULLING_ENGINE_API void SetLogCallback(PFN_CullingEngineLogCallback callback);

/*
 * Controls whether log messages are stored in the internal message buffer.
 *
 * Parameters:
 *   enabled - True to store messages for later retrieval through GetLog.
 *             False disables that internal message buffer.
 */
CULLING_ENGINE_API void SetStoreMessages(bool enabled);

/*
 * Adds a preformatted message to the log pipeline.
 *
 * Parameters:
 *   message  - Message text to record.
 *   filename - Source file associated with the message. May be nullptr.
 *   line     - Source line associated with the message.
 */
CULLING_ENGINE_API void AddLog(std::string_view message, const char* filename,
                               int line);

/*
 * Retrieves one stored log message.
 *
 * Parameters:
 *   msgOut   - Destination buffer for a null-terminated message.
 *   capacity - Size of msgOut in bytes.
 *
 * Returns:
 *   true when a message was copied, false when no message is available or
 *   the output buffer is invalid.
 */
CULLING_ENGINE_API bool GetLog(char* msgOut, std::size_t capacity = 256);

/*
 * Flushes stored log messages through the configured output path.
 *
 * Returns:
 *   true when at least one message was printed, false when the buffer was
 *   empty.
 */
CULLING_ENGINE_API bool PrintLog();

/*
 * Records a message with explicit severity and source location.
 *
 * Parameters:
 *   level    - Severity level for the message.
 *   filename - Source file associated with the message. May be nullptr.
 *   line     - Source line associated with the message.
 *   message  - Message text to record.
 */
CULLING_ENGINE_API void Log(Level level, const char* filename, int line,
                            std::string_view message);

/*
 * Formats and records a message using a va_list.
 *
 * Parameters:
 *   level    - Severity level for the message.
 *   filename - Source file associated with the message. May be nullptr.
 *   line     - Source line associated with the message.
 *   format   - printf-style format string.
 *   args     - Arguments for format.
 */
CULLING_ENGINE_API void VLogFormatted(Level level, const char* filename,
                                      int line, const char* format,
                                      va_list args);

/*
 * Formats and records a message using printf-style arguments.
 *
 * Parameters:
 *   level    - Severity level for the message.
 *   filename - Source file associated with the message. May be nullptr.
 *   line     - Source line associated with the message.
 *   format   - printf-style format string followed by matching arguments.
 */
CULLING_ENGINE_API void LogFormatted(Level level, const char* filename,
                                     int line, const char* format, ...);
} /* namespace CullingEngine */
