# P2-6: Windows Signal Handling

## Objective

Add SIGINT/SIGTERM signal handling on Windows so servers can shut down gracefully when stopped via Ctrl+C or service manager.

## Current State

`engine.cc:219-238` wraps signal handling in `#ifndef _WIN32`:

```cpp
#ifndef _WIN32
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
#endif
```

Windows servers cannot be stopped gracefully — the process is forcefully terminated, potentially corrupting database state and losing player data.

## Implementation Steps

### Step 1: Add Windows Console Control Handler

**File**: `src/runtime/engine/engine.cc`

```cpp
#ifdef _WIN32
#include <windows.h>

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:        /* Ctrl+C */
    case CTRL_CLOSE_EVENT:    /* Closing console window */
    case CTRL_SHUTDOWN_EVENT: /* System shutdown */
    case CTRL_LOGOFF_EVENT:   /* User logoff */
        Engine::Instance().SignalShutdown();
        return TRUE;  /* Signal handled */
    default:
        return FALSE;
    }
}

/* In Engine::Init() — replaces the #ifndef _WIN32 block */
void Engine::InstallSignalHandlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
#endif
}
#endif
```

### Step 2: Tests

- Manual test: run on Windows, press Ctrl+C, verify graceful shutdown log messages
- On Linux: existing signal handling tests continue to pass

## Acceptance Criteria

1. Ctrl+C on Windows triggers `Engine::SignalShutdown()`
2. Console window close triggers graceful shutdown
3. Graceful shutdown completes all cleanup steps (database flush, network close, etc.)
4. Platform-specific code is cleanly ifdef'd

## Dependencies: None | Estimated Effort: ~50 lines
