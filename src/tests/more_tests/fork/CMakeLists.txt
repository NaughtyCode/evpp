file(GLOB SROUCES *.cc *.h)

if (UNIX)
    add_executable(test_evpp_after_fork ${SROUCES})
    set_target_properties(test_evpp_after_fork PROPERTIES FOLDER "tests")
    target_link_libraries(test_evpp_after_fork engine ${DEPENDENT_LIBRARIES})
endif (UNIX)

