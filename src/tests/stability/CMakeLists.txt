file(GLOB SOURCES CONFIGURE_DEPENDS *.cc *.h)
add_executable(test_evpp_stability ${SOURCES})
set_target_properties(test_evpp_stability PROPERTIES FOLDER "tests")
target_link_libraries(test_evpp_stability CloudEngine)

if (UNIX)
    add_executable(test_evpp_stability_boost_lockfree ${SOURCES})
    set_target_properties(test_evpp_stability_boost_lockfree PROPERTIES FOLDER "tests")
    target_link_libraries(test_evpp_stability_boost_lockfree CloudEngine)

    add_executable(test_evpp_stability_concurrentqueue ${SOURCES})
    set_target_properties(test_evpp_stability_concurrentqueue PROPERTIES FOLDER "tests")
    target_link_libraries(test_evpp_stability_concurrentqueue CloudEngine)
endif (UNIX)

