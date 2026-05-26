cmake_minimum_required(VERSION 4.0)

project(GameServer C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# ── Platform settings ────────────────────────────────────────────────
if (UNIX)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
    set (CXX_FLAGS
         -g
         -fPIC
         -Wall
         -D__STDC_FORMAT_MACROS
         -Wno-unused-parameter
         -Wno-unused-function
         -Wunused-variable
         -Wunused-value
         -Wshadow
         -Wcast-qual
         -Wcast-align
         -Wwrite-strings
         -Wsign-compare
         -Winvalid-pch
         -fms-extensions
         -Wfloat-equal
         -Wextra
    )
else (UNIX)
    set (CXX_FLAGS -DWIN32 -D_WIN32 -EHsc /utf-8 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif (UNIX)

string (REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if (UNIX)
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -D_DEBUG")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG")
endif (UNIX)

if (HTTPS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
endif (HTTPS)

if (CMAKE_BENCHMARK_TESTING)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif (CMAKE_BENCHMARK_TESTING)

# ── Output paths ──────────────────────────────────────────────────────
set (EXECUTABLE_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/bin)
set (LIBRARY_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/lib)
set (CMAKE_MACOSX_RPATH 0)

# ── Status ────────────────────────────────────────────────────────────
message (STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message (STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
message (STATUS "CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS})
message (STATUS "EXECUTABLE_OUTPUT_PATH " ${EXECUTABLE_OUTPUT_PATH})
message (STATUS "LIBRARY_OUTPUT_PATH " ${LIBRARY_OUTPUT_PATH})

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
