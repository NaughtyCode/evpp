
add_executable(example_c10m_client main.cc)
target_link_libraries(example_c10m_client ${LIBRARIES} gflags)

set_target_properties(example_c10m_client PROPERTIES FOLDER "tests/examples")

