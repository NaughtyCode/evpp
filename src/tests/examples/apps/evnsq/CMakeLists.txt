include_directories(${ENGINE_ROOT}/tests/apps ${ENGINE_ROOT}/thirdparty)

set(LIBRARIES CloudEngine)

add_executable(example_evnsq_tail evnsq_tail/main.cc)
target_link_libraries(example_evnsq_tail evnsq_static ${LIBRARIES})

set (evnsq_producer_SRCS evnsq_producer/main.cc)
if (WIN32)
	list (APPEND evnsq_producer_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt.c)
    list (APPEND evnsq_producer_SRCS ${ENGINE_ROOT}/thirdparty/win32code/getopt/getopt_long.c)
endif (WIN32)

add_executable(example_evnsq_producer ${evnsq_producer_SRCS})
target_link_libraries(example_evnsq_producer evnsq_static ${LIBRARIES})

set_target_properties(example_evnsq_tail example_evnsq_producer PROPERTIES FOLDER "tests/examples")
