file(GLOB SRCS *.cc *.h)

add_executable(example_recipes_self_control_timer_cancel_03 ${SRCS})
target_link_libraries(example_recipes_self_control_timer_cancel_03 event)

set_target_properties(example_recipes_self_control_timer_cancel_03 PROPERTIES FOLDER "tests/examples")



