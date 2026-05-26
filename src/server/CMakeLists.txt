cmake_minimum_required(VERSION 4.0)

project(GameServer C CXX)

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# Import CloudEngine target from the runtime project (skipped if already
# available, e.g. when building via the combined top-level CMakeLists.txt).
if(NOT TARGET CloudEngine)
    add_subdirectory(${ENGINE_ROOT}/runtime ${CMAKE_BINARY_DIR}/runtime)
endif()

# ═══════════════════════════════════════════════════════════════════════
# GameServer executable
# ═══════════════════════════════════════════════════════════════════════

add_executable(GameServer server.cc)
set_target_properties(GameServer PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(GameServer CloudEngine)
if(WIN32)
    target_link_libraries(GameServer ws2_32)
endif()
