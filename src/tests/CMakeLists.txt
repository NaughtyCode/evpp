get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Redirect all test executables to artifacts/tests
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ENGINE_ROOT}/../artifacts/tests)


include_directories(${ENGINE_ROOT}/thirdparty
                    ${ENGINE_ROOT}/thirdparty/gtest)

include(CTest)
add_test(NAME evpp_unittest COMMAND evpp_unittest)

option(BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_BENCHMARKS "Build benchmark programs" OFF)

add_subdirectory(unit)
add_subdirectory(smoke)
add_subdirectory(apps)
add_subdirectory(labs)
add_subdirectory(stability)
add_subdirectory(misc)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
