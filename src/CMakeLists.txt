cmake_minimum_required(VERSION 4.0)

project(CloudEngine C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ── Platform definitions (MSVC) ───────────────────────────────────────
# Directory-scoped so all subprojects (runtime, tests, server, client)
# inherit these definitions.
if(MSVC)
    add_compile_definitions(NOMINMAX _WIN32)
    add_compile_options(/utf-8 /bigobj /Zc:preprocessor)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── Output paths ──────────────────────────────────────────────────────
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../artifacts/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../artifacts/lib)
set(CMAKE_MACOSX_RPATH 0)

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

# ── Copy resources to output ──────────────────────────────────────────
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources"
    "${EXECUTABLE_OUTPUT_PATH}/$<CONFIG>/resources"
    COMMENT "Copying resources/ to bin/$<CONFIG>/"
)

# ── Sub-projects ──────────────────────────────────────────────────────
# Platform flags, third-party setup, and options are owned by each
# sub-project.  This file is a thin wrapper for building all projects
# together in a single build tree.

add_subdirectory(server)
add_subdirectory(client)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT GameServer)
