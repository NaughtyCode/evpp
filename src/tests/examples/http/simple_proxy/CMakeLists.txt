file(GLOB example_SRCS CONFIGURE_DEPENDS *.cc)

add_executable(example_http_simple_proxy ${example_SRCS})
target_link_libraries(example_http_simple_proxy ${LIBRARIES})

set_target_properties(example_http_simple_proxy PROPERTIES FOLDER "tests/examples")


