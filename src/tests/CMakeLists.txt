cmake_minimum_required(VERSION 4.0)

# ═══════════════════════════════════════════════════════════════════════════
# CloudEngine Test Suite
# ═══════════════════════════════════════════════════════════════════════════

# ── Catch2 v3 (header-only) ──────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0
)
FetchContent_MakeAvailable(Catch2)

# ── Google Benchmark (optional) ─────────────────────────────────────
option(CLOUDENGINE_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(CLOUDENGINE_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

# ── Test library — shared fixtures compiled into a static lib ───────
# ENGINE_ROOT is set by the parent CMakeLists.txt when BUILD_TESTING=ON
if(NOT DEFINED ENGINE_ROOT)
    get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(ENGINE_MONGODB_ENABLED)
        message(STATUS "ENGINE_MONGODB_ENABLED forced OFF on mobile platform ${CMAKE_SYSTEM_NAME}")
    endif()
    set(ENGINE_MONGODB_ENABLED OFF CACHE BOOL "Enable MongoDB driver support" FORCE)
endif()

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

if(ENGINE_PHYSICS_ENABLED AND NOT TARGET Jolt)
    configure_engine_jolt_physics(${ENGINE_ROOT})
endif()

set(ENGINE_EMBEDDED_TEST_CONFIG_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/embedded_test_config.h"
)

# ── Common include directories for all test targets ──────────────────
set(TEST_COMMON_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${ENGINE_ROOT}"
    "${ENGINE_ROOT}/client"
    "${ENGINE_ROOT}/runtime"
    "${ENGINE_ROOT}/thirdparty/libevent/include"
    "${CMAKE_BINARY_DIR}/libevent/include"
    "${ENGINE_ROOT}/thirdparty/quill/quill/include"
    "${ENGINE_ROOT}/thirdparty/lua/src"
    "${ENGINE_ROOT}/thirdparty/glaze/glaze/include"
    "${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue"
)

# ── Embedded runtime-under-test and shared target wiring ─────────────
if(TARGET bson_static)
    list(APPEND TEST_COMMON_INCLUDES
        "${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libbson/src"
        "${CMAKE_BINARY_DIR}/mongo-c-driver/src/libbson/src"
    )
endif()
if(TARGET mongoc_static)
    list(APPEND TEST_COMMON_INCLUDES
        "${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libmongoc/src"
        "${CMAKE_BINARY_DIR}/mongo-c-driver/src/libmongoc/src"
    )
endif()
if(ENGINE_PHYSICS_ENABLED)
    list(APPEND TEST_COMMON_INCLUDES
        "${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics"
    )
endif()
if(HTTPS)
    list(APPEND TEST_COMMON_INCLUDES
        "${ENGINE_ROOT}/thirdparty/boringssl/include"
    )
endif()

if(UNIX)
    set(ENGINE_EMBEDDED_TEST_PLATFORM_LIBS event pthread)
else()
    set(ENGINE_EMBEDDED_TEST_PLATFORM_LIBS event_core_static event_extra_static)
endif()

set(ENGINE_EMBEDDED_TEST_LINK_LIBS
    engine_lua_static
    engine_kcp_static
    ${ENGINE_EMBEDDED_TEST_PLATFORM_LIBS}
)

if(ENGINE_PROFILER_ENABLED)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS engine_perfetto_static)
endif()
if(ENGINE_PHYSICS_ENABLED)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS Jolt)
endif()
if(TARGET bson_static)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS bson_static)
endif()
if(TARGET mongoc_static)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS mongoc_static)
    if(WIN32)
        list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS secur32 crypt32 Bcrypt ncrypt Dnsapi)
    endif()
endif()
if(HTTPS)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS ssl crypto)
endif()
if(WIN32)
    list(APPEND ENGINE_EMBEDDED_TEST_LINK_LIBS ws2_32)
endif()

add_library(engine_runtime_test_objects OBJECT ${SERVERENGINE_SOURCES})
target_include_directories(engine_runtime_test_objects PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(engine_runtime_test_objects PRIVATE ${ENGINE_EMBEDDED_TEST_LINK_LIBS})
target_compile_definitions(engine_runtime_test_objects PRIVATE
    ENGINE_BUILD
    ENGINE_AUTOMATED_TESTS=1
    ENGINE_EMBEDDED_TESTS=1
    ENGINE_TEST_RUNTIME=1
)

if(WIN32)
    target_compile_definitions(engine_runtime_test_objects PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(ENGINE_PHYSICS_ENABLED)
    target_compile_definitions(engine_runtime_test_objects PRIVATE ENGINE_PHYSICS_ENABLED)
endif()
if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(engine_runtime_test_objects PRIVATE ENGINE_PROFILER_ENABLED)
endif()
if(ENGINE_MONGODB_ENABLED)
    target_compile_definitions(engine_runtime_test_objects PRIVATE ENGINE_MONGODB_ENABLED)
endif()
if(ENGINE_MEM_STATS_ENABLED)
    target_compile_definitions(engine_runtime_test_objects PRIVATE ENGINE_MEM_STATS_ENABLED)
endif()

set_target_properties(engine_runtime_test_objects PROPERTIES FOLDER "tests/embedded")

set(ENGINE_CLIENT_API_TEST_SOURCES
    ${ENGINE_ROOT}/client/client.cpp
    ${ENGINE_ROOT}/client/client_log.cpp
    ${ENGINE_ROOT}/client/client_runtime.cpp
    ${ENGINE_ROOT}/client/client_timer.cpp
    ${ENGINE_ROOT}/client/client_net.cpp
    ${ENGINE_ROOT}/client/client_udp.cpp
    ${ENGINE_ROOT}/client/client_kcp.cpp
)

function(configure_engine_test_entry name suite_macro)
    string(MAKE_C_IDENTIFIER "${name}" test_target_id)
    string(TOUPPER "${test_target_id}" test_target_id)

    target_include_directories(${name} PRIVATE ${TEST_COMMON_INCLUDES})
    target_compile_definitions(${name} PRIVATE
        ENGINE_AUTOMATED_TESTS=1
        ENGINE_EMBEDDED_TESTS=1
        ${suite_macro}=1
        "ENGINE_TEST_TARGET_${test_target_id}=1"
    )

    if(WIN32)
        target_compile_definitions(${name} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WIN32_WINNT=0x0A00
            _CRT_SECURE_NO_WARNINGS
        )
    endif()

    if(MSVC)
        target_compile_options(${name} PRIVATE
            "/FI${ENGINE_EMBEDDED_TEST_CONFIG_HEADER}"
        )
    else()
        target_compile_options(${name} PRIVATE
            "-include" "${ENGINE_EMBEDDED_TEST_CONFIG_HEADER}"
        )
    endif()
endfunction()

function(configure_embedded_runtime_test_target name suite_macro)
    configure_engine_test_entry(${name} ${suite_macro})
    target_sources(${name} PRIVATE $<TARGET_OBJECTS:engine_runtime_test_objects>)
    target_link_directories(${name} PRIVATE "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
    target_link_libraries(${name} PRIVATE ${ENGINE_EMBEDDED_TEST_LINK_LIBS})
    if(ENGINE_PHYSICS_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_PHYSICS_ENABLED)
    endif()
    if(ENGINE_PROFILER_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_PROFILER_ENABLED)
    endif()
    if(ENGINE_MONGODB_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_MONGODB_ENABLED)
    endif()
    if(ENGINE_MEM_STATS_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_MEM_STATS_ENABLED)
    endif()
endfunction()

function(configure_embedded_client_api_test_target name suite_macro)
    configure_engine_test_entry(${name} ${suite_macro})
    target_sources(${name} PRIVATE
        ${ENGINE_CLIENT_API_TEST_SOURCES}
        $<TARGET_OBJECTS:engine_runtime_test_objects>
    )
    target_compile_definitions(${name} PRIVATE CLIENT_BUILD)
    target_link_directories(${name} PRIVATE "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
    target_link_libraries(${name} PRIVATE ${ENGINE_EMBEDDED_TEST_LINK_LIBS})
    if(ENGINE_PHYSICS_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_PHYSICS_ENABLED)
    endif()
    if(ENGINE_PROFILER_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_PROFILER_ENABLED)
    endif()
    if(ENGINE_MONGODB_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_MONGODB_ENABLED)
    endif()
    if(ENGINE_MEM_STATS_ENABLED)
        target_compile_definitions(${name} PRIVATE ENGINE_MEM_STATS_ENABLED)
    endif()
endfunction()

# Tests and resources are deployed to artifacts/bin/<config>/.
set(TEST_WORKING_DIR "${EXECUTABLE_OUTPUT_PATH}/$<CONFIG>")

# ── Sub-directories ─────────────────────────────────────────────────
add_subdirectory(smoke)
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(lua)

if(CLOUDENGINE_BUILD_BENCHMARKS)
    add_subdirectory(performance)
endif()

# ── CTest configuration ─────────────────────────────────────────────
# Test properties (timeout, labels) are set in each subdirectory's
# CMakeLists.txt right after add_test() to avoid scoping issues.
