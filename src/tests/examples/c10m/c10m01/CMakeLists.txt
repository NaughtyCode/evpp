
add_executable(example_c10m01 main.cc)
target_link_libraries(example_c10m01 ${LIBRARIES})

set_target_properties(example_c10m01 PROPERTIES FOLDER "tests/examples")

