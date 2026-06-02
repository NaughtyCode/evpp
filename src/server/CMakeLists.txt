cmake_minimum_required(VERSION 4.0)

project(GameServer C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" ON)
option(ENGINE_MONGODB_ENABLED "Enable MongoDB driver support" ON)
option(ENGINE_PHYSICS_ENABLED "Enable Jolt Physics integration" ON)
option(ENGINE_MEM_STATS_ENABLED "Enable memory allocation statistics tracking" OFF)
option(HTTPS "Enable HTTPS/SSL support" OFF)
option(CMAKE_BENCHMARK_TESTING "Enable benchmark testing" OFF)
option(ENGINE_WERROR "Treat compiler warnings as errors" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(ENGINE_MONGODB_ENABLED)
        message(STATUS "ENGINE_MONGODB_ENABLED forced OFF on mobile platform ${CMAKE_SYSTEM_NAME}")
    endif()
    set(ENGINE_MONGODB_ENABLED OFF CACHE BOOL "Enable MongoDB driver support" FORCE)
endif()

if(UNIX)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
    set(CXX_FLAGS
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
else()
    set(CXX_FLAGS -DWIN32 -D_WIN32 -EHsc /utf-8 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if(UNIX)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG")
endif()

if(ENGINE_WERROR)
    if(MSVC)
        message(STATUS "ENGINE_WERROR=ON: /WX enabled")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
        message(STATUS "ENGINE_WERROR=ON: -Werror enabled")
    endif()
endif()

if(HTTPS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_OPENSSL_ENABLED")

    if(NOT TARGET ssl)
        set(OPENSSL_NO_ASM ON CACHE BOOL "Disable assembly when NASM not available")
        add_subdirectory(${ENGINE_ROOT}/thirdparty/boringssl
                         ${CMAKE_BINARY_DIR}/boringssl EXCLUDE_FROM_ALL)
        set_property(TARGET crypto PROPERTY FOLDER "thirdparty")
        set_property(TARGET ssl PROPERTY FOLDER "thirdparty")
    endif()
endif()

if(CMAKE_BENCHMARK_TESTING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif()

if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00 -D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/bigobj /Zc:preprocessor /utf-8 /W4)
    add_compile_options(/wd4100 /wd4127 /wd4201 /wd4251 /wd4503)

    if(ENGINE_WERROR)
        add_compile_options(/WX)
    endif()

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(ENGINE_PLATFORM_LIBS event)
elseif(UNIX)
    set(ENGINE_PLATFORM_LIBS event pthread)
else()
    set(ENGINE_PLATFORM_LIBS event_core_static event_extra_static)
endif()

set(EXECUTABLE_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/bin)
set(LIBRARY_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/lib)
set(CMAKE_MACOSX_RPATH 0)

if(CMAKE_CONFIGURATION_TYPES)
    set(ENGINE_RUNTIME_OUTPUT_DIR "${EXECUTABLE_OUTPUT_PATH}/$<CONFIG>")
else()
    set(ENGINE_RUNTIME_OUTPUT_DIR "${EXECUTABLE_OUTPUT_PATH}")
endif()

if(NOT TARGET copy_resources)
    add_custom_target(copy_resources ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ENGINE_ROOT}/../resources"
        "${ENGINE_RUNTIME_OUTPUT_DIR}/resources"
        COMMENT "Copying resources/ to runtime output directory"
    )
endif()

message(STATUS "This is BINARY dir ${PROJECT_BINARY_DIR}")
message(STATUS "This is SOURCE dir ${PROJECT_SOURCE_DIR}")
message(STATUS "CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}")
message(STATUS "EXECUTABLE_OUTPUT_PATH ${EXECUTABLE_OUTPUT_PATH}")
message(STATUS "LIBRARY_OUTPUT_PATH ${LIBRARY_OUTPUT_PATH}")

include(${ENGINE_ROOT}/runtime/CMakeLists.txt)
include(${ENGINE_ROOT}/cmake/JoltPhysics.cmake)

if(NOT TARGET engine_lua_static)
    add_subdirectory(${ENGINE_ROOT}/thirdparty/lua ${CMAKE_BINARY_DIR}/lua)
endif()

if(NOT TARGET engine_kcp_static)
    add_subdirectory(${ENGINE_ROOT}/thirdparty/kcp ${CMAKE_BINARY_DIR}/kcp EXCLUDE_FROM_ALL)
endif()

if(ENGINE_PROFILER_ENABLED AND NOT TARGET engine_perfetto_static)
    add_subdirectory(${ENGINE_ROOT}/thirdparty/perfetto ${CMAKE_BINARY_DIR}/perfetto EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET event_core_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(EVENT__HAVE_PIPE2 OFF CACHE INTERNAL "pipe2 is not declared on all supported Android API levels" FORCE)
    endif()
    set(EVENT__DISABLE_TESTS ON CACHE BOOL "Disable libevent tests")
    set(EVENT__DISABLE_REGRESS ON CACHE BOOL "Disable libevent regression tests")
    set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "Disable libevent samples")
    set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "Disable libevent benchmarks")
    set(EVENT__DISABLE_OPENSSL ON CACHE STRING "Disable OpenSSL support")
    set(EVENT__DISABLE_MBEDTLS ON CACHE STRING "Disable MbedTLS support")
    set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "Build libevent as static library")
    set(EVENT__HAVE_WEPOLL OFF CACHE BOOL "Disable wepoll backend")
    add_subdirectory(${ENGINE_ROOT}/thirdparty/libevent ${CMAKE_BINARY_DIR}/libevent)
    set_property(TARGET event_core_static event_extra_static event_static PROPERTY FOLDER "thirdparty")
    set_property(TARGET event_core event_extra event PROPERTY FOLDER "thirdparty")
endif()

if(ENGINE_MONGODB_ENABLED AND NOT TARGET bson_static)
    set(ENABLE_MONGOC ON CACHE BOOL "" FORCE)
    set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
    set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZLIB BUNDLED CACHE STRING "" FORCE)
    set(ENABLE_ZSTD OFF CACHE STRING "" FORCE)
    set(ENABLE_SNAPPY OFF CACHE STRING "" FORCE)
    set(ENABLE_SRV ON CACHE BOOL "" FORCE)
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENABLE_CLIENT_SIDE_ENCRYPTION OFF CACHE BOOL "" FORCE)
    set(ENABLE_MONGODB_AWS_AUTH OFF CACHE BOOL "" FORCE)
    set(ENABLE_UNINSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_MAN_PAGES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HTML_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TRACING OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(ENABLE_SSL WINDOWS CACHE STRING "" FORCE)
        set(ENABLE_SASL SSPI CACHE STRING "" FORCE)
    else()
        set(ENABLE_SSL AUTO CACHE STRING "" FORCE)
        set(ENABLE_SASL AUTO CACHE STRING "" FORCE)
    endif()
    add_subdirectory(${ENGINE_ROOT}/thirdparty/mongo-c-driver
                     ${CMAKE_BINARY_DIR}/mongo-c-driver)
    if(MSVC)
        target_compile_options(bson_static PRIVATE /utf-8)
        if(TARGET zlib_obj)
            target_compile_options(zlib_obj PRIVATE /utf-8)
        endif()
        if(TARGET utf8proc_obj)
            target_compile_options(utf8proc_obj PRIVATE /utf-8)
        endif()
        if(TARGET mongoc_static)
            target_compile_options(mongoc_static PRIVATE /utf-8)
        endif()
    endif()
    set_property(TARGET bson_static PROPERTY FOLDER "thirdparty")
    if(TARGET zlib_obj)
        set_property(TARGET zlib_obj PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET utf8proc_obj)
        set_property(TARGET utf8proc_obj PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mongoc_static)
        set_property(TARGET mongoc_static PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET kms_message_static)
        set_property(TARGET kms_message_static PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET public-header-warnings)
        set_property(TARGET public-header-warnings PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mcd_rpc)
        set_property(TARGET mcd_rpc PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mlib-ckdint-test)
        set_property(TARGET mlib-ckdint-test PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mongo_c_driver_examples)
        set_property(TARGET mongo_c_driver_examples PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mongo_c_driver_tests)
        set_property(TARGET mongo_c_driver_tests PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mongoc-cxx-check)
        set_property(TARGET mongoc-cxx-check PROPERTY FOLDER "thirdparty")
    endif()
endif()

if(ENGINE_PHYSICS_ENABLED AND NOT TARGET Jolt)
    configure_engine_jolt_physics(${ENGINE_ROOT})
endif()

set(ENGINE_RUNTIME_INCLUDE_DIRS
    ${ENGINE_ROOT}
    ${ENGINE_ROOT}/runtime
    ${ENGINE_ROOT}/thirdparty/libevent/include
    ${CMAKE_BINARY_DIR}/libevent/include
    ${ENGINE_ROOT}/thirdparty/quill/quill/include
    ${ENGINE_ROOT}/thirdparty/lua/src
    ${ENGINE_ROOT}/thirdparty/glaze/glaze/include
    ${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue
)

if(TARGET bson_static)
    list(APPEND ENGINE_RUNTIME_INCLUDE_DIRS
        ${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libbson/src
        ${CMAKE_BINARY_DIR}/mongo-c-driver/src/libbson/src
    )
endif()
if(TARGET mongoc_static)
    list(APPEND ENGINE_RUNTIME_INCLUDE_DIRS
        ${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libmongoc/src
        ${CMAKE_BINARY_DIR}/mongo-c-driver/src/libmongoc/src
    )
endif()
if(ENGINE_PHYSICS_ENABLED)
    list(APPEND ENGINE_RUNTIME_INCLUDE_DIRS
        ${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics
    )
endif()
if(HTTPS)
    list(APPEND ENGINE_RUNTIME_INCLUDE_DIRS
        ${ENGINE_ROOT}/thirdparty/boringssl/include
    )
endif()

set(ENGINE_RUNTIME_LINK_LIBS
    engine_lua_static
    engine_kcp_static
    ${ENGINE_PLATFORM_LIBS}
)

if(ENGINE_PROFILER_ENABLED)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS engine_perfetto_static)
endif()
if(ENGINE_PHYSICS_ENABLED)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS Jolt)
endif()
if(TARGET bson_static)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS bson_static)
endif()
if(TARGET mongoc_static)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS mongoc_static)
    if(WIN32)
        list(APPEND ENGINE_RUNTIME_LINK_LIBS secur32 crypt32 Bcrypt ncrypt Dnsapi)
    endif()
endif()
if(HTTPS)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS ssl crypto)
endif()
if(WIN32)
    list(APPEND ENGINE_RUNTIME_LINK_LIBS ws2_32)
endif()

add_executable(GameServer
    server.cc
    ${CLOUD_ENGINE_SOURCES}
)

add_dependencies(GameServer copy_resources)
set_target_properties(GameServer PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

target_include_directories(GameServer PRIVATE ${ENGINE_RUNTIME_INCLUDE_DIRS})
target_compile_definitions(GameServer PRIVATE ENGINE_BUILD)

if(ENGINE_PHYSICS_ENABLED)
    target_compile_definitions(GameServer PRIVATE ENGINE_PHYSICS_ENABLED)
endif()
if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(GameServer PRIVATE ENGINE_PROFILER_ENABLED)
endif()
if(ENGINE_MONGODB_ENABLED)
    target_compile_definitions(GameServer PRIVATE ENGINE_MONGODB_ENABLED)
endif()
if(ENGINE_MEM_STATS_ENABLED)
    target_compile_definitions(GameServer PRIVATE ENGINE_MEM_STATS_ENABLED)
endif()

target_link_directories(GameServer PRIVATE "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
target_link_libraries(GameServer PRIVATE ${ENGINE_RUNTIME_LINK_LIBS})
