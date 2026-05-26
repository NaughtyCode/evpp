set (evpp_SRCS evpp_ioevent_bench.cc)
if (WIN32)
    list (APPEND evpp_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt.c)
    list (APPEND evpp_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt_long.c)
endif (WIN32)
add_executable(benchmark_ioevent_evpp ${evpp_SRCS})
target_link_libraries(benchmark_ioevent_evpp CloudEngine)



set (evpp_SRCS evpp_ioevent_pipe_watcher.cc)
if (WIN32)
    list (APPEND evpp_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt.c)
    list (APPEND evpp_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt_long.c)
endif (WIN32)

add_executable(benchmark_ioevent_evpp_pipe_watcher ${evpp_SRCS})
target_link_libraries(benchmark_ioevent_evpp_pipe_watcher CloudEngine)

set_target_properties(benchmark_ioevent_evpp benchmark_ioevent_evpp_pipe_watcher PROPERTIES FOLDER "tests/benchmarks")


