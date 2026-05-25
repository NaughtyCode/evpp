#include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/thirdparty/gtest ${PROJECT_SOURCE_DIR}/thirdparty)
#
#if (MSVC)
#link_directories(${PROJECT_SOURCE_DIR}/vsprojects/bin/${CMAKE_BUILD_TYPE}/
#				 ${LIBRARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/
#				 ${PROJECT_SOURCE_DIR}/thirdparty/glog-0.3.4/${CMAKE_BUILD_TYPE})
#endif (MSVC)

file(GLOB SROUCES *.cc *.h)
add_executable(test_evpp_stability ${SROUCES})
set_target_properties(test_evpp_stability PROPERTIES FOLDER "tests")
target_link_libraries(test_evpp_stability CloudEngine ${DEPENDENT_LIBRARIES})

if (UNIX)
    add_executable(test_evpp_stability_boost_lockfree ${SROUCES})
    target_link_libraries(test_evpp_stability_boost_lockfree CloudEngine ${DEPENDENT_LIBRARIES})

    add_executable(test_evpp_stability_concurrentqueue ${SROUCES})
    target_link_libraries(test_evpp_stability_concurrentqueue CloudEngine ${DEPENDENT_LIBRARIES})
endif (UNIX)

