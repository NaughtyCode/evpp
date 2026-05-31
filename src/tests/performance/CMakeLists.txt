# ═══════════════════════════════════════════════════════════════════════════
# Performance benchmarks — Google Benchmark
# ═══════════════════════════════════════════════════════════════════════════

set(BENCH_LIBS benchmark::benchmark_main)

macro(add_bench name)
    add_executable(${name} ${ARGN})
    configure_embedded_runtime_test_target(${name} ENGINE_TEST_SUITE_PERFORMANCE)
    target_link_libraries(${name} PRIVATE ${BENCH_LIBS})
    set_target_properties(${name} PROPERTIES FOLDER "tests/performance")
endmacro()

add_bench(bench_buffer bench_buffer.cpp)
add_test(NAME performance.bench_buffer COMMAND bench_buffer
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_config bench_config.cpp)
add_test(NAME performance.bench_config COMMAND bench_config
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_timer bench_timer.cpp)
add_test(NAME performance.bench_timer COMMAND bench_timer
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_aoi bench_aoi.cpp)
add_test(NAME performance.bench_aoi COMMAND bench_aoi
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_msgpack bench_msgpack.cpp)
add_test(NAME performance.bench_msgpack COMMAND bench_msgpack
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_scriptvm bench_scriptvm.cpp)
add_test(NAME performance.bench_scriptvm COMMAND bench_scriptvm
    --benchmark_min_time=0.1s --benchmark_format=console)

add_bench(bench_tcp bench_tcp_throughput.cpp)
add_test(NAME performance.bench_tcp COMMAND bench_tcp
    --benchmark_min_time=0.5s --benchmark_format=console)
