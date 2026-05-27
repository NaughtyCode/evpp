#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "client/client.h"
}

#include <cstdint>
#include <cstring>

TEST_CASE("client_api: ABI handle pointer size", "[client_api]") {
    // game_client_t is an opaque handle; verify pointer type is well-formed.
    STATIC_CHECK(sizeof(game_client_t*) == sizeof(void*));
}

TEST_CASE("client_api: version string", "[client_api]") {
    const char* ver = game_version();
    REQUIRE(ver != nullptr);
    REQUIRE(std::strlen(ver) > 0);
}

TEST_CASE("client_api: error code values", "[client_api]") {
    STATIC_CHECK(GAME_OK == 0);
    STATIC_CHECK(GAME_ERR_INVALID_ARG < 0);
    STATIC_CHECK(GAME_ERR_NETWORK < 0);
}

TEST_CASE("client_api: create and destroy", "[client_api]") {
    game_client_t* c = nullptr;
    game_error_t rc = game_client_create(&c);
    REQUIRE(rc == GAME_OK);
    REQUIRE(c != nullptr);
    game_client_destroy(&c);
    REQUIRE(c == nullptr);
}
