file(GLOB SRCS CONFIGURE_DEPENDS *.cc *.h)

add_executable(example_recipes_self_control_timer_basic_01 ${SRCS})
target_include_directories(example_recipes_self_control_timer_basic_01 PRIVATE
    ${ENGINE_ROOT}
    ${ENGINE_ROOT}/thirdparty/libevent/include
    ${CMAKE_BINARY_DIR}/libevent/include)
if(UNIX)
    target_link_libraries(example_recipes_self_control_timer_basic_01 event pthread)
else()
    target_link_libraries(example_recipes_self_control_timer_basic_01 event_core_static event_extra_static)
endif()

set_target_properties(example_recipes_self_control_timer_basic_01 PROPERTIES FOLDER "tests/examples")
