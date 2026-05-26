include_directories(${ENGINE_ROOT}/tests/apps
                    ${ENGINE_ROOT}/tests/labs
                    ${ENGINE_ROOT}/thirdparty)

if (UNIX)
set(LIBRARIES CloudEngine event pthread)
else()
set(LIBRARIES CloudEngine event_core_static event_extra_static)
endif()


if(THRIFT_COMPILER AND THRIFT_INCLUDE_DIR AND THRIFT_LIBRARY)
    add_subdirectory(evthrift)
endif()

