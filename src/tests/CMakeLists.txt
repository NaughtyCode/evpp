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

# ── Common include directories for all test targets ──────────────────
set(TEST_COMMON_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${ENGINE_ROOT}"
    "${ENGINE_ROOT}/runtime"
    "${ENGINE_ROOT}/thirdparty/libevent/include"
    "${CMAKE_BINARY_DIR}/libevent/include"
    "${ENGINE_ROOT}/thirdparty/quill/quill/include"
    "${ENGINE_ROOT}/thirdparty/lua/src"
    "${ENGINE_ROOT}/thirdparty/glaze/glaze/include"
    "${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue"
)

# ── Common working directory for all tests ──────────────────────────
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
