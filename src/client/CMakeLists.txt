add_library(GameClient SHARED
    src/client.cpp
    src/client_log.cpp
    src/client_timer.cpp
    src/client_net.cpp
    src/client_udp.cpp
    src/client_kcp.cpp
)

target_include_directories(GameClient
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/thirdparty/lua/lua"
)

target_compile_definitions(GameClient PRIVATE CLIENT_BUILD)

target_link_libraries(GameClient ServerEngine)

if(WIN32)
    target_link_libraries(GameClient ws2_32)
endif()

# ── Examples ────────────────────────────────────────────────────────────

option(CLIENT_BUILD_EXAMPLES "Build GameClient example programs" ON)
if(CLIENT_BUILD_EXAMPLES)
    add_executable(client_example ${PROJECT_SOURCE_DIR}/tests/game_client/basic_demo.c)
    target_include_directories(client_example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(client_example GameClient)
endif()

# ── Install ─────────────────────────────────────────────────────────────

install(TARGETS GameClient
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES include/client.h DESTINATION include)
