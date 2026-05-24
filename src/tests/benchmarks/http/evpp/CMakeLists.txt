
if (UNIX)
else(UNIX)
link_directories(${PROJECT_SOURCE_DIR}/vsprojects/bin/${CMAKE_BUILD_TYPE}/
                 ${LIBRARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/
                 ${PROJECT_SOURCE_DIR}/thirdparty/glog-0.3.4/${CMAKE_BUILD_TYPE})
endif(UNIX)
				 
add_executable(benchmark_http_evpp evpp_http_bench.cc )
target_link_libraries(benchmark_http_evpp ServerEngine ${DEPENDENT_LIBRARIES})

set_target_properties(benchmark_http_evpp PROPERTIES FOLDER "tests/benchmarks")

