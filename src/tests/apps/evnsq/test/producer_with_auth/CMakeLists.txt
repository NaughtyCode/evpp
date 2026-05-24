
set (unittest_evnsq_producer_with_auth_SRCS main.cc)
if (WIN32)
	list (APPEND unittest_evnsq_producer_with_auth_SRCS ${PROJECT_SOURCE_DIR}/thirdparty/win32code/getopt/getopt.c)
    list (APPEND unittest_evnsq_producer_with_auth_SRCS ${PROJECT_SOURCE_DIR}/thirdparty/win32code/getopt/getopt_long.c)
endif (WIN32)

add_executable(unittest_evnsq_producer_with_auth ${unittest_evnsq_producer_with_auth_SRCS})
target_link_libraries(unittest_evnsq_producer_with_auth evnsq_static ${LIBRARIES})
set_property(TARGET unittest_evnsq_producer_with_auth PROPERTY FOLDER "tests/apps")

