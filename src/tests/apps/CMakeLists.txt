include_directories(${PROJECT_SOURCE_DIR}/tests/apps ${PROJECT_SOURCE_DIR}/thirdparty)

if (UNIX)
    set(LIBRARIES ServerEngine event pthread)
else(UNIX)
    set(LIBRARIES ServerEngine event)
endif(UNIX)

# All test apps output under artifacts/tests
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../artifacts/tests)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../artifacts/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../artifacts/lib)

link_directories(${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

add_subdirectory(evnsq)
add_subdirectory(evmc)
