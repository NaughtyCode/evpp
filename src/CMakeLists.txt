cmake_minimum_required(VERSION 3.8)

project (safe-evpp C CXX)

option(ENGINE_PHYSICS_ENABLED "Enable the JoltPhysics engine subsystem" ON)
option(BUILD_EXAMPLES "Build example programs (requires NOT EVPP_VCPKG_BUILD)" OFF)
option(BUILD_BENCHMARKS "Build benchmark programs (requires NOT EVPP_VCPKG_BUILD)" OFF)

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
         -std=c++11
    )
    include_directories (${PROJECT_SOURCE_DIR}
                         ${PROJECT_SOURCE_DIR}/thirdparty)
else (UNIX)
set (CXX_FLAGS -DWIN32 -D_WIN32 -EHsc /utf-8)
    set(VcpkgRoot ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET})
    include_directories(${VcpkgRoot}/include
                        ${PROJECT_SOURCE_DIR}
                        ${PROJECT_SOURCE_DIR}/thirdparty/wininclude)

    # On windows, vcpkg uses different lib names for debug (eventd.lib) vs release (event.lib)
    link_directories(${VcpkgRoot}/debug/lib ${VcpkgRoot}/debug/lib/manual-link
                     ${VcpkgRoot}/lib ${VcpkgRoot}/lib/manual-link)
endif (UNIX)

string (REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if (UNIX)
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -D_DEBUG -DGOOGLE_STRIP_LOG=0")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG -DGOOGLE_STRIP_LOG=1")
    SET(DEPENDENT_LIBRARIES event glog pthread)
else (UNIX)
    find_library(EVENT_LIBRARY event REQUIRED)
    find_library(GLOG_LIBRARY glog REQUIRED)
    SET(DEPENDENT_LIBRARIES ${EVENT_LIBRARY} ${GLOG_LIBRARY})
    # Suppress evpp's per-frame DLOG_TRACE noise (OnTimerTriggered etc.) in Release
    SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DGOOGLE_STRIP_LOG=1")
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

add_subdirectory (runtime)
add_subdirectory (tests)
add_subdirectory (server)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/build/cmake")
include (utils)
include (packages)

include (CPack)
