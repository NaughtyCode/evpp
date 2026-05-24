file(GLOB SRCS *.cc *.h)

add_executable(example_recipes_self_control_timer_periodic_04 ${SRCS})
target_link_libraries(example_recipes_self_control_timer_periodic_04 event)

set_target_properties(example_recipes_self_control_timer_periodic_04 PROPERTIES FOLDER "tests/examples")



