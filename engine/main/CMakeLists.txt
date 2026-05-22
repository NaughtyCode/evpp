set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/3rdparty/quill/include)

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

if (UNIX)
    set(LIBRARIES evpp_concurrentqueue event glog pthread)
else (UNIX)
    set(LIBRARIES evpp_static event glog)
endif (UNIX)

add_executable(engine_main
    main.cc
    ../core/log/log.cc
)

target_link_libraries(engine_main ${LIBRARIES})
