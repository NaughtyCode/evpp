include_directories("../../../")

add_executable(benchmark_gettimeofday gettimeofday.cc)

set_target_properties(benchmark_gettimeofday PROPERTIES FOLDER "tests/benchmarks")
