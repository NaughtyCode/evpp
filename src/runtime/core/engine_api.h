#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Platform detection
// ═══════════════════════════════════════════════════════════════════════════

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#ifndef H_OS_WINDOWS
#define H_OS_WINDOWS
#endif
#ifndef H_WINDOWS_API
#define H_WINDOWS_API
#endif
#endif

#if defined(__APPLE__)
#define H_OS_MACOSX
#endif

#ifdef _DEBUG
#ifndef H_DEBUG_MODE
#define H_DEBUG_MODE
#endif
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Windows platform adapters
// ═══════════════════════════════════════════════════════════════════════════

#ifdef H_OS_WINDOWS
#define usleep(us) Sleep((us) / 1000)
// snprintf and thread_local are provided by modern Windows SDK / MSVC;
// defining them as macros conflicts with the compiler and triggers error C1189.
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PRIu64 — platform-specific printf format specifier for uint64_t
// ═══════════════════════════════════════════════════════════════════════════

#include <inttypes.h>
#ifndef PRIu64
#ifdef H_OS_WINDOWS
#define PRIu64 "I64u"
#else
#define PRIu64 "lu"
#endif
#endif

// ═══════════════════════════════════════════════════════════════════════════
// DLL export / import
// ═══════════════════════════════════════════════════════════════════════════

// ENGINE_API — used by engine-layer classes (physics, vm, script, profiler, etc.)
// EVPP_EXPORT — used by evpp network classes (EventLoop, Buffer, TCPClient, etc.)
// Both are compiled into CloudEngine.dll. ENGINE_BUILD is defined by
// runtime/CMakeLists.txt when building the library.

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef ENGINE_BUILD
#define ENGINE_API __declspec(dllexport)
#define EVPP_EXPORT __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#define EVPP_EXPORT __declspec(dllimport)
#endif
#else
#if __GNUC__ >= 4
#define ENGINE_API __attribute__((visibility("default")))
#define EVPP_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_API
#define EVPP_EXPORT
#endif
#endif
