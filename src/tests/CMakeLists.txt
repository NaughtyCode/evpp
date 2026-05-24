file(GLOB evpp_unittest_SRCS *.cc
							 ${PROJECT_SOURCE_DIR}/3rdparty/gtest/src/gtest-all.cc
							 ${PROJECT_SOURCE_DIR}/3rdparty/gtest/src/gtest_main.cc)
list(REMOVE_ITEM evpp_unittest_SRCS ${PROJECT_SOURCE_DIR}/tests/https_client_test.cc ${PROJECT_SOURCE_DIR}/tests/physics_smoke.cc)
file(GLOB evpp_https_unittest_SRCS https_client_test.cc
							 ${PROJECT_SOURCE_DIR}/3rdparty/gtest/src/gtest-all.cc
							 ${PROJECT_SOURCE_DIR}/3rdparty/gtest/src/gtest_main.cc)
include_directories(${PROJECT_SOURCE_DIR}/3rdparty ${PROJECT_SOURCE_DIR}/3rdparty/gtest)

if (MSVC)
link_directories(${LIBRARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/)
endif (MSVC)

add_executable(evpp_unittest ${evpp_unittest_SRCS})
set_target_properties(evpp_unittest PROPERTIES FOLDER "tests")
target_link_libraries(evpp_unittest evpp_static ${DEPENDENT_LIBRARIES})

if (HTTPS)
add_executable(evpp_https_unittest ${evpp_https_unittest_SRCS})
set_target_properties(evpp_https_unittest PROPERTIES FOLDER "tests")
target_link_libraries(evpp_https_unittest evpp_https_static ${DEPENDENT_LIBRARIES})
endif (HTTPS)

if (UNIX)
    add_executable(evpp_unittest_boost_lockfree ${evpp_unittest_SRCS})
    target_link_libraries(evpp_unittest_boost_lockfree evpp_boost ${DEPENDENT_LIBRARIES})

    add_executable(evpp_unittest_concurrentqueue ${evpp_unittest_SRCS})
    target_link_libraries(evpp_unittest_concurrentqueue evpp_concurrentqueue ${DEPENDENT_LIBRARIES})
endif (UNIX)

include (CTest)
add_test(NAME evpp_unittest COMMAND evpp_unittest)

if (UNIX)
    add_test(NAME evpp_unittest_concurrentqueue COMMAND evpp_unittest_concurrentqueue)
    add_test(NAME evpp_unittest_boost_lockfree  COMMAND evpp_unittest_boost_lockfree)
endif (UNIX)

# ── Physics smoke test ────────────────────────────────────────────────
add_executable(physics_smoke physics_smoke.cc)
set_target_properties(physics_smoke PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON FOLDER "tests")
target_compile_definitions(physics_smoke PRIVATE ENGINE_PHYSICS_ENABLED)
target_include_directories(physics_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/3rdparty/JoltPhysics/JoltPhysics
    ${PROJECT_SOURCE_DIR}/3rdparty/glaze/include
    ${PROJECT_SOURCE_DIR}/3rdparty/quill/quill/include
    ${PROJECT_SOURCE_DIR}/3rdparty/concurrentqueue/concurrentqueue
    ${PROJECT_SOURCE_DIR}/3rdparty/lua/lua
)
target_link_libraries(physics_smoke engine_physics Jolt)

add_subdirectory (apps)
add_subdirectory (labs)
add_subdirectory (stability)
add_subdirectory (more_tests)

if (BUILD_EXAMPLES)
    add_subdirectory (examples)
endif()
if (BUILD_BENCHMARKS)
    add_subdirectory (benchmarks)
endif()
