cmake_minimum_required(VERSION 4.0)

project (ServerEngine C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENGINE_PHYSICS_ENABLED "Enable the JoltPhysics engine subsystem" ON)
option(BUILD_EXAMPLES "Build example programs (requires NOT EVPP_VCPKG_BUILD)" OFF)
option(BUILD_BENCHMARKS "Build benchmark programs (requires NOT EVPP_VCPKG_BUILD)" OFF)
option(BUILD_CLIENT "Build GameClient library" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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
    include_directories (${PROJECT_SOURCE_DIR}
                         ${PROJECT_SOURCE_DIR}/runtime
                         ${PROJECT_SOURCE_DIR}/thirdparty
                         ${PROJECT_SOURCE_DIR}/thirdparty/libevent/include
                         ${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include
                         ${PROJECT_SOURCE_DIR}/thirdparty/concurrentqueue/concurrentqueue
                         ${PROJECT_SOURCE_DIR}/thirdparty/glaze/glaze/include)
else (UNIX)
    set (CXX_FLAGS -DWIN32 -D_WIN32 -EHsc /utf-8 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    include_directories(${PROJECT_SOURCE_DIR}
                        ${PROJECT_SOURCE_DIR}/runtime
                        ${PROJECT_SOURCE_DIR}/thirdparty/wininclude
                        ${PROJECT_SOURCE_DIR}/thirdparty/libevent/include
                        ${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include
                        ${PROJECT_SOURCE_DIR}/thirdparty/concurrentqueue/concurrentqueue
                        ${PROJECT_SOURCE_DIR}/thirdparty/glaze/glaze/include)
endif (UNIX)

string (REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if (UNIX)
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -D_DEBUG")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG")
    SET(DEPENDENT_LIBRARIES event pthread)
else (UNIX)
    SET(DEPENDENT_LIBRARIES event)
endif (UNIX)

# Set to true if https support is needed.
# Note that this needs openssl
# SET(HTTPS True)
if (HTTPS)
    list(APPEND DEPENDENT_LIBRARIES ssl crypto event_openssl)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
endif (HTTPS)


if (CMAKE_BENCHMARK_TESTING)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif (CMAKE_BENCHMARK_TESTING)

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../artifacts/bin)
set (LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../artifacts/lib)

#set (MACOSX_RPATH ${LIBRARY_OUTPUT_PATH})
set (CMAKE_MACOSX_RPATH 0)

find_program(THRIFT_COMPILER thrift)
find_path(THRIFT_INCLUDE_DIR thrift)
find_library(THRIFT_LIBRARY NAMES thrift)

message (STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message (STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
message (STATUS "CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS})
message (STATUS "EXECUTABLE_OUTPUT_PATH " ${EXECUTABLE_OUTPUT_PATH})
message (STATUS "LIBRARY_OUTPUT_PATH " ${LIBRARY_OUTPUT_PATH})
message (STATUS "THRIFT_COMPILER " ${THRIFT_COMPILER})
message (STATUS "THRIFT_INCLUDE_DIR " ${THRIFT_INCLUDE_DIR})
message (STATUS "THRIFT_LIBRARY " ${THRIFT_LIBRARY})

# ── libevent (built from source) ──────────────────────────────────────
set(EVENT__DISABLE_TESTS ON CACHE BOOL "Disable libevent tests")
set(EVENT__DISABLE_REGRESS ON CACHE BOOL "Disable libevent regression tests")
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "Disable libevent samples")
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "Disable libevent benchmarks")
set(EVENT__DISABLE_OPENSSL ON CACHE STRING "Disable OpenSSL support")
set(EVENT__DISABLE_MBEDTLS ON CACHE STRING "Disable MbedTLS support")
set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "Build libevent as static library")
set(EVENT__HAVE_WEPOLL OFF CACHE BOOL "Disable wepoll backend")
add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/libevent ${PROJECT_BINARY_DIR}/libevent)
include_directories(${PROJECT_BINARY_DIR}/libevent/include)

set_property(TARGET event_core_static event_extra_static event_static PROPERTY FOLDER "thirdparty")
set_property(TARGET event_core event_extra event PROPERTY FOLDER "thirdparty")

add_subdirectory (runtime)
add_subdirectory (tests)
add_subdirectory (server)
if(BUILD_CLIENT)
    add_subdirectory (client)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT main)

# CPack packaging (requires src/build/cmake/{utils,packages}.cmake)
# set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/build/cmake")
# include (utils)
# include (packages)
# include (CPack)
