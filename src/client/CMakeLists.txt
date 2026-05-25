# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
# Core VM
set(GAMECLIENT_LUA_CORE
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lapi.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lcode.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lctype.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ldebug.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ldo.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ldump.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lfunc.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lgc.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/llex.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lmem.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lobject.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lopcodes.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lparser.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lstate.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lstring.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ltable.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ltm.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lundump.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lvm.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lzio.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lstate_custom_ptr.c
)
# Standard libraries
set(GAMECLIENT_LUA_LIB
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lauxlib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lbaselib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lcorolib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ldblib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/liolib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lmathlib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/loadlib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/loslib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lstrlib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/ltablib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/lutf8lib.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/src/linit.c
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
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/thirdparty/lua/src"
)

target_compile_definitions(GameClient PRIVATE CLIENT_BUILD)

target_link_libraries(GameClient CloudEngine)

if(WIN32)
    target_link_libraries(GameClient ws2_32)
endif()

add_custom_command(TARGET GameClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:GameClient>"
    "${PROJECT_SOURCE_DIR}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:GameClient>"
    COMMENT "Copying GameClient.dll to bin/"
)

# ── Examples ────────────────────────────────────────────────────────────

option(CLIENT_BUILD_EXAMPLES "Build GameClient example programs" ON)
if(CLIENT_BUILD_EXAMPLES)
    add_executable(client_example ${PROJECT_SOURCE_DIR}/tests/game_client/basic_demo.c)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/tests/game_client/basic_demo.c
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
