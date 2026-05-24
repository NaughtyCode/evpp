#pragma once

#include <quill/core/LogLevel.h>

namespace engine {

using LogLevel = quill::LogLevel;

// Quill 9-level → game 6-level mapping:
//   TraceL3/TraceL2 → not exposed externally
//   TraceL1         → TRACE   verbose tracing
//   Debug           → DEBUG   development debugging
//   Info            → INFO    key business flow
//   Notice          → not used
//   Warning         → WARN    recoverable error
//   Error           → ERROR   non-recoverable, current operation failed
//   Critical        → FATAL   process is about to exit

constexpr auto kDefaultLevel = LogLevel::Info;

} // namespace engine
