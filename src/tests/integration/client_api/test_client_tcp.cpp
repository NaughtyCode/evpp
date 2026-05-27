#include <catch2/catch_test_macros.hpp>
#include <client.h>

#include <cstring>

TEST_CASE("Client API handle size is pointer-sized", "[integration][client][abi]") {
    REQUIRE(sizeof(game_client_t*) == sizeof(void*));
}

TEST_CASE("Client API version string is non-empty", "[integration][client][abi]") {
    const char* version = game_version();
    REQUIRE(version != nullptr);
    REQUIRE(std::strlen(version) > 0);
}

TEST_CASE("Client API create and destroy", "[integration][client][lifecycle]") {
    game_client_t* client = nullptr;
    game_error_t rc = game_client_create(&client);
    REQUIRE(rc == GAME_OK);
    REQUIRE(client != nullptr);
    game_client_destroy(&client);
    REQUIRE(client == nullptr);
}

TEST_CASE("Client API error codes are distinct", "[integration][client][abi]") {
    REQUIRE(GAME_OK == 0);
    REQUIRE(GAME_ERR_INVALID_ARG != GAME_OK);
    REQUIRE(GAME_ERR_NETWORK != GAME_OK);
}
