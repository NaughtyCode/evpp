include_directories(${ENGINE_ROOT} ${ENGINE_ROOT}/thirdparty)

set(LIBRARIES CloudEngine)

add_executable(benchmark_pingpong_header_body_client client.cc)
target_link_libraries(benchmark_pingpong_header_body_client ${LIBRARIES})

add_executable(benchmark_pingpong_header_body_server server.cc)
target_link_libraries(benchmark_pingpong_header_body_server ${LIBRARIES})

set_target_properties(benchmark_pingpong_header_body_client benchmark_pingpong_header_body_server PROPERTIES FOLDER "tests/benchmarks")
