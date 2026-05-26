include_directories(${ENGINE_ROOT} ${ENGINE_ROOT}/thirdparty)

set(LIBRARIES CloudEngine)

add_executable(benchmark_pingpong_client client.cc)
target_link_libraries(benchmark_pingpong_client ${LIBRARIES})

add_executable(benchmark_pingpong_client_fixed_size client_fixed_size.cc)
target_link_libraries(benchmark_pingpong_client_fixed_size ${LIBRARIES})

add_executable(benchmark_pingpong_server server.cc)
target_link_libraries(benchmark_pingpong_server ${LIBRARIES})

set_target_properties(benchmark_pingpong_client benchmark_pingpong_client_fixed_size benchmark_pingpong_server PROPERTIES FOLDER "tests/benchmarks")
