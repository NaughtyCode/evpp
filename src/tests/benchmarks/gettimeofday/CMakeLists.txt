include_directories(${ENGINE_ROOT})

add_executable(benchmark_gettimeofday gettimeofday.cc)

set_target_properties(benchmark_gettimeofday PROPERTIES FOLDER "tests/benchmarks")
