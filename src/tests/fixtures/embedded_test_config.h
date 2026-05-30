#pragma once

#ifndef ENGINE_AUTOMATED_TESTS
#error "Automated tests must define ENGINE_AUTOMATED_TESTS."
#endif

#ifndef ENGINE_EMBEDDED_TESTS
#error "Automated tests must define ENGINE_EMBEDDED_TESTS."
#endif

#if !defined(ENGINE_TEST_SUITE_UNIT) && !defined(ENGINE_TEST_SUITE_SMOKE) && \
    !defined(ENGINE_TEST_SUITE_INTEGRATION) && !defined(ENGINE_TEST_SUITE_LUA) && \
    !defined(ENGINE_TEST_SUITE_PERFORMANCE)
#error "Embedded test target must define an ENGINE_TEST_SUITE_* macro."
#endif

#define ENGINE_TEST_ONLY(...) __VA_ARGS__
#define ENGINE_TEST_DISABLED(...) static_assert(true, "test code disabled")

#ifdef __cplusplus
namespace engine::test_config {
inline constexpr bool kAutomated = true;
inline constexpr bool kEmbedded = true;

#if defined(ENGINE_TEST_SUITE_UNIT)
inline constexpr const char* kSuite = "unit";
#elif defined(ENGINE_TEST_SUITE_SMOKE)
inline constexpr const char* kSuite = "smoke";
#elif defined(ENGINE_TEST_SUITE_INTEGRATION)
inline constexpr const char* kSuite = "integration";
#elif defined(ENGINE_TEST_SUITE_LUA)
inline constexpr const char* kSuite = "lua";
#elif defined(ENGINE_TEST_SUITE_PERFORMANCE)
inline constexpr const char* kSuite = "performance";
#endif
}  // namespace engine::test_config
#endif
