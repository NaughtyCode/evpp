cmake_minimum_required(VERSION 4.0)

project(GameClient C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENGINE_MONGODB_ENABLED "Enable MongoDB driver support" OFF)
option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" OFF)
option(ENGINE_PHYSICS_ENABLED "Enable Jolt Physics integration" OFF)
option(HTTPS "Enable HTTPS/SSL support" OFF)
option(CMAKE_BENCHMARK_TESTING "Enable benchmark testing" OFF)
option(ENGINE_WERROR "Treat compiler warnings as errors" OFF)

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

if(ENGINE_WERROR)
    if(MSVC)
        message(STATUS "ENGINE_WERROR=ON: /WX enabled")
    else()
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
        message(STATUS "ENGINE_WERROR=ON: -Werror enabled")
    endif()
endif()

if (HTTPS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
	    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_OPENSSL_ENABLED")

    # ── BoringSSL (bundled) ──────────────────────────────────────────
    if(NOT TARGET ssl)
        set(OPENSSL_NO_ASM ON CACHE BOOL "Disable assembly when NASM not available")
        add_subdirectory(${ENGINE_ROOT}/thirdparty/boringssl
                         ${CMAKE_BINARY_DIR}/boringssl EXCLUDE_FROM_ALL)
        set_property(TARGET crypto PROPERTY FOLDER "thirdparty")
        set_property(TARGET ssl PROPERTY FOLDER "thirdparty")
    endif()
endif (HTTPS)

if (CMAKE_BENCHMARK_TESTING)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif (CMAKE_BENCHMARK_TESTING)

# ── MSVC compile options ────────────────────────────────────────────────
if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_compile_options(/bigobj /Zc:preprocessor /utf-8)

    # Enable warning level 4 (equivalent to -Wall -Wextra)
    add_compile_options(/W4)

    # Minimal warning suppressions — only noise that cannot be fixed in code:
    add_compile_options(/wd4100 /wd4127 /wd4201 /wd4503)

    if(ENGINE_WERROR)
        add_compile_options(/WX)
    endif()

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── Platform libraries (from runtime) ─────────────────────────────────
if(UNIX)
    set(LIBRARIES event pthread)
else()
    set(LIBRARIES event_core_static event_extra_static)
endif()

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

# ── Runtime source-file organization ──────────────────────────────────
# Provides SERVERENGINE_SOURCES and per-module source lists.
include(${ENGINE_ROOT}/runtime/CMakeLists.txt)

# ═══════════════════════════════════════════════════════════════════════
# Third-party dependencies
# ═══════════════════════════════════════════════════════════════════════

# ── libevent (built from source) ──────────────────────────────────────
if(NOT TARGET event_core_static)
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
include_directories(${CMAKE_BINARY_DIR}/libevent/include)

# ── mongo-c-driver (static, bundled deps) ─────────────────────────────
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
    if (WIN32)
        set(ENABLE_SSL WINDOWS CACHE STRING "" FORCE)
        set(ENABLE_SASL SSPI CACHE STRING "" FORCE)
    else ()
        set(ENABLE_SSL AUTO CACHE STRING "" FORCE)
        set(ENABLE_SASL AUTO CACHE STRING "" FORCE)
    endif ()
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

# ── JoltPhysics ───────────────────────────────────────────────────────
if(ENGINE_PHYSICS_ENABLED AND NOT TARGET Jolt)
    add_subdirectory(${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics/Build jolt)
    set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# ═══════════════════════════════════════════════════════════════════════
# GameClient library (runtime sources integrated at source level)
# ═══════════════════════════════════════════════════════════════════════

add_library(GameClient SHARED
    client.cpp
    client_log.cpp
    client_timer.cpp
    client_net.cpp
    client_udp.cpp
    client_kcp.cpp
    ${SERVERENGINE_SOURCES}
)

add_dependencies(GameClient copy_resources)

target_include_directories(GameClient
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
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
    target_include_directories(GameClient PRIVATE
        ${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libbson/src
        ${CMAKE_BINARY_DIR}/mongo-c-driver/src/libbson/src
    )
endif()
if(TARGET mongoc_static)
    target_include_directories(GameClient PRIVATE
        ${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libmongoc/src
        ${CMAKE_BINARY_DIR}/mongo-c-driver/src/libmongoc/src
    )
endif()

if(ENGINE_PHYSICS_ENABLED)
    target_include_directories(GameClient PRIVATE
        ${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics
    )
endif()

target_compile_definitions(GameClient PRIVATE ENGINE_BUILD CLIENT_BUILD)

if(ENGINE_PHYSICS_ENABLED)
    target_compile_definitions(GameClient PUBLIC ENGINE_PHYSICS_ENABLED)
endif()

if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(GameClient PUBLIC ENGINE_PROFILER_ENABLED)
endif()

if(ENGINE_MONGODB_ENABLED)
    target_compile_definitions(GameClient PUBLIC ENGINE_MONGODB_ENABLED)
endif()

# ── Link libraries ───────────────────────────────────────────────────
if(HTTPS)
    target_include_directories(GameClient PRIVATE
        ${ENGINE_ROOT}/thirdparty/boringssl/include)
    target_link_libraries(GameClient PRIVATE ssl crypto)
endif()

target_link_directories(GameClient PRIVATE "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
target_link_libraries(GameClient PRIVATE ${LIBRARIES})

if(ENGINE_PHYSICS_ENABLED)
    target_link_libraries(GameClient PRIVATE Jolt)
endif()

if(TARGET bson_static)
    target_link_libraries(GameClient PRIVATE bson_static)
endif()
if(TARGET mongoc_static)
    target_link_libraries(GameClient PRIVATE mongoc_static)
    if(WIN32)
        target_link_libraries(GameClient PRIVATE secur32 crypt32 Bcrypt ncrypt Dnsapi)
    endif()
endif()

if(WIN32)
    target_link_libraries(GameClient PRIVATE ws2_32)
endif()

add_custom_command(TARGET GameClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:GameClient>"
    "${ENGINE_ROOT}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:GameClient>"
    COMMENT "Copying GameClient.dll to bin/"
)

# ── Install ─────────────────────────────────────────────────────────────

install(TARGETS GameClient
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES client.h DESTINATION include)
