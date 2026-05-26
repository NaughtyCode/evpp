file(GLOB SOURCES CONFIGURE_DEPENDS *.cc *.h)

if (UNIX)
    add_executable(test_evpp_after_fork ${SOURCES})
    set_target_properties(test_evpp_after_fork PROPERTIES FOLDER "tests")
    target_link_libraries(test_evpp_after_fork CloudEngine)
endif (UNIX)

