option(EVPP_VCPKG_BUILD "Building via vcpkg" OFF)

include_directories(${ENGINE_ROOT}/tests/apps ${ENGINE_ROOT}/thirdparty)

if (UNIX)
    set(LIBRARIES CloudEngine event pthread)
else()
    set(LIBRARIES CloudEngine event_core_static event_extra_static)
endif()

# All test apps output under artifacts/tests
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ENGINE_ROOT}/../artifacts/tests)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ENGINE_ROOT}/../artifacts/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ENGINE_ROOT}/../artifacts/lib)

link_directories(${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

add_subdirectory(evnsq)
add_subdirectory(evmc)
