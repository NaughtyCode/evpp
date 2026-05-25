cmake_minimum_required(VERSION 3.20)
project(GameClient VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build options ───────────────────────────────────────────────────────

option(CLIENT_BUILD_EXAMPLES "Build example programs" ON)

# ── Locate ServerEngine ─────────────────────────────────────────────────
#
# The C API wraps ServerEngine. You need the ServerEngine headers and the
# import library (or DLL) available. By default we assume the standard
# project layout:
#   src/runtime/   — ServerEngine headers
#   artifacts/lib/ — ServerEngine.lib / libServerEngine.so
#
# Override EVPP_SRC_DIR and EVPP_ARTIFACTS_DIR if your layout differs.

set(EVPP_SRC_DIR      "${CMAKE_SOURCE_DIR}"             CACHE PATH "Path to src/ (contains runtime/ headers)")
set(EVPP_ARTIFACTS_DIR "${CMAKE_SOURCE_DIR}/../artifacts" CACHE PATH "Path to artifacts/ (contains bin/ and lib/)")

# ── Source files ────────────────────────────────────────────────────────

set(CLIENT_SOURCES
    src/client.cpp
    src/client_log.cpp
    src/client_timer.cpp
    src/client_net.cpp
    src/client_udp.cpp
    src/client_kcp.cpp
)

set(CLIENT_HEADERS
    include/client.h
    src/client_internal.h
)

# ── Build shared library ────────────────────────────────────────────────

add_library(GameClient SHARED ${CLIENT_SOURCES} ${CLIENT_HEADERS})

target_include_directories(GameClient
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${EVPP_SRC_DIR}"
        "${EVPP_SRC_DIR}/thirdparty/lua/lua"
)

target_compile_definitions(GameClient PRIVATE CLIENT_BUILD)

# Link against ServerEngine. On Windows, link the import library.
if(WIN32)
    target_link_libraries(GameClient
        "${EVPP_ARTIFACTS_DIR}/lib/ServerEngine.lib"
        ws2_32
    )
else()
    target_link_libraries(GameClient
        "${EVPP_ARTIFACTS_DIR}/lib/libServerEngine.so"
        pthread
    )
endif()

# ── Install ─────────────────────────────────────────────────────────────

install(TARGETS GameClient
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES include/client.h DESTINATION include)

# ── Examples ────────────────────────────────────────────────────────────

if(CLIENT_BUILD_EXAMPLES)
    add_executable(client_example ../tests/game_client/basic_demo.c)
    target_include_directories(client_example PRIVATE include)
    target_link_libraries(client_example GameClient)
endif()
