# ============================================================
# CullingEngine Test Project - Independent CMake
# Can be built as a subproject or standalone
# ============================================================

# Support standalone builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(CullingEngineTests VERSION 1.6.0 LANGUAGES CXX)

    find_package(CullingEngine QUIET)
    if(NOT CullingEngine_FOUND)
        message(FATAL_ERROR "CullingEngine library not found. Build CullingEngine first or set CullingEngine_DIR.")
    endif()
endif()

if(TARGET CullingEngine::CullingEngine)
    set(CullingEngine_TEST_TARGET CullingEngine::CullingEngine)
else()
    set(CullingEngine_TEST_TARGET CullingEngine)
endif()

if(NOT TARGET doctest::doctest)
    add_library(CullingEngine_doctest INTERFACE)
    target_include_directories(CullingEngine_doctest
        INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/doctest
    )
    add_library(doctest::doctest ALIAS CullingEngine_doctest)
endif()

function(ConfigureCullingEngineTestTarget target_name)
    target_link_libraries(${target_name} PRIVATE ${CullingEngine_TEST_TARGET})

    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W3 /wd4996)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()

    target_compile_features(${target_name} PUBLIC cxx_std_20)

    get_target_property(CullingEngine_TEST_TARGET_TYPE ${CullingEngine_TEST_TARGET} TYPE)
    if(WIN32 AND CullingEngine_TEST_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${CullingEngine_TEST_TARGET}>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
endfunction()

# ============================================================
# Replay / smoke executable
# ============================================================
add_executable(CullingEngine_replay_test
    replay/main.cpp
    replay/CullingEngineReplayTest.cpp
    replay/CullingEngineReplayTest.h
)
ConfigureCullingEngineTestTarget(CullingEngine_replay_test)
target_include_directories(CullingEngine_replay_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/replay)

# ============================================================
# Unit executable
# ============================================================
add_executable(CullingEngine_unit_tests
    unit/test_main.cpp
    unit/api/CullingEngineApiTests.cpp
    unit/baking/MeshBakerTests.cpp
    unit/debug/LoggingAndCaptureTests.cpp
    unit/math/MathUtilityTests.cpp
    unit/rendering/OcclusionWorkflowTests.cpp
)
ConfigureCullingEngineTestTarget(CullingEngine_unit_tests)
target_link_libraries(CullingEngine_unit_tests PRIVATE doctest::doctest)
target_include_directories(CullingEngine_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit/common)
if(MSVC)
    target_compile_options(CullingEngine_unit_tests PRIVATE /wd5285)
endif()

enable_testing()
add_test(NAME CullingEngine_unit_tests COMMAND CullingEngine_unit_tests)
add_test(NAME CullingEngine_replay_smoke COMMAND CullingEngine_replay_test --smoke)

message(STATUS "CullingEngine tests configured")
