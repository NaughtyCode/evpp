file(GLOB example_SRCS CONFIGURE_DEPENDS *.cc)

add_executable(example_evpphttp_server ${example_SRCS})
target_link_libraries(example_evpphttp_server ${LIBRARIES})

set_target_properties(example_evpphttp_server PROPERTIES FOLDER "tests/examples")


