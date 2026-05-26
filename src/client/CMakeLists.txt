cmake_minimum_required(VERSION 4.0)

project(GameClient C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# Import CloudEngine target from the runtime project (skipped if already
# available, e.g. when building via the combined top-level CMakeLists.txt).
if(NOT TARGET CloudEngine)
    add_subdirectory(${ENGINE_ROOT}/runtime ${CMAKE_BINARY_DIR}/runtime)
endif()

# ═══════════════════════════════════════════════════════════════════════
# GameClient library
# ═══════════════════════════════════════════════════════════════════════

# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
# Core VM
set(GAMECLIENT_LUA_CORE
    ${ENGINE_ROOT}/thirdparty/lua/src/lapi.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lcode.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lctype.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldebug.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldo.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldump.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lfunc.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lgc.c
    ${ENGINE_ROOT}/thirdparty/lua/src/llex.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lmem.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lobject.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lopcodes.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lparser.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstate.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstring.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltable.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltm.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lundump.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lvm.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lzio.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstate_custom_ptr.c
)
# Standard libraries
set(GAMECLIENT_LUA_LIB
    ${ENGINE_ROOT}/thirdparty/lua/src/lauxlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lbaselib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lcorolib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldblib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/liolib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lmathlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/loadlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/loslib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstrlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltablib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lutf8lib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/linit.c
)

add_library(GameClient SHARED
    client.cpp
    client_log.cpp
    client_timer.cpp
    client_net.cpp
    client_udp.cpp
    client_kcp.cpp
    ${GAMECLIENT_LUA_CORE}
    ${GAMECLIENT_LUA_LIB}
)

target_include_directories(GameClient
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${ENGINE_ROOT}"
        "${ENGINE_ROOT}/thirdparty/lua/src"
)

target_compile_definitions(GameClient PRIVATE CLIENT_BUILD)

target_link_libraries(GameClient CloudEngine)

if(WIN32)
    target_link_libraries(GameClient ws2_32)
endif()

add_custom_command(TARGET GameClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:GameClient>"
    "${ENGINE_ROOT}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:GameClient>"
    COMMENT "Copying GameClient.dll to bin/"
)

# ── Examples ────────────────────────────────────────────────────────────

option(CLIENT_BUILD_EXAMPLES "Build GameClient example programs" ON)
if(CLIENT_BUILD_EXAMPLES)
    add_executable(client_example ${ENGINE_ROOT}/tests/game_client/basic_demo.c)
    set_source_files_properties(${ENGINE_ROOT}/tests/game_client/basic_demo.c
        PROPERTIES LANGUAGE CXX)
    target_include_directories(client_example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(client_example GameClient)
endif()

# ── Install ─────────────────────────────────────────────────────────────

install(TARGETS GameClient
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES client.h DESTINATION include)
