add_library(GameClient SHARED
    client.cpp
    client_log.cpp
    client_timer.cpp
    client_net.cpp
    client_udp.cpp
    client_kcp.cpp
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua/onelua.c
)

target_include_directories(GameClient
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/thirdparty/lua/lua"
)

target_compile_definitions(GameClient PRIVATE CLIENT_BUILD)

set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua/onelua.c
    PROPERTIES COMPILE_DEFINITIONS "MAKE_LIB"
)

target_link_libraries(GameClient ServerEngine)

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
