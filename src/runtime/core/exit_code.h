#pragma once

namespace engine {

// Structured exit codes for the server process.
// Mapped to OS process exit codes in main().
enum class ExitCode : int {
    Success                = 0,
    GenericError           = 1,
    ConfigNotFound         = 2,
    ConfigParseError       = 3,
    ConfigValidationError  = 4,
    PermissionDenied       = 5,
    PortInUse              = 6,
    MultiInstance          = 7,   // PID file already locked
};

}  // namespace engine
