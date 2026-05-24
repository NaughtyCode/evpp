file(GLOB SRCS *.cc *.h)

add_executable(example_recipes_self_control_timer_basic_01 ${SRCS})
target_link_libraries(example_recipes_self_control_timer_basic_01 event)

set_target_properties(example_recipes_self_control_timer_basic_01 PROPERTIES FOLDER "tests/examples")




