#pragma once

// Shared test helpers for the CloudEngine test suite.

#include <string>
#include <catch2/catch_test_macros.hpp>

namespace test {

// Assert that a string contains a substring.
inline void AssertContains(const std::string& haystack, const std::string& needle,
                           const std::string& msg = "") {
    if (!msg.empty()) { UNSCOPED_INFO(msg); }
    REQUIRE(haystack.find(needle) != std::string::npos);
}

// Assert that a string does not contain a substring.
inline void AssertNotContains(const std::string& haystack, const std::string& needle,
                              const std::string& msg = "") {
    if (!msg.empty()) { UNSCOPED_INFO(msg); }
    REQUIRE(haystack.find(needle) == std::string::npos);
}

}  // namespace test
