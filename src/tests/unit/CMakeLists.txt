include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB evpp_unittest_SRCS *.cc
     ${PROJECT_SOURCE_DIR}/thirdparty/gtest/src/gtest-all.cc
     ${PROJECT_SOURCE_DIR}/thirdparty/gtest/src/gtest_main.cc)

list(REMOVE_ITEM evpp_unittest_SRCS
     ${CMAKE_CURRENT_SOURCE_DIR}/https_client_test.cc
     ${CMAKE_CURRENT_SOURCE_DIR}/glog_test.cc)

if(MSVC)
    link_directories(${LIBRARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/)
endif()

add_executable(evpp_unittest ${evpp_unittest_SRCS})
set_target_properties(evpp_unittest PROPERTIES FOLDER "tests")
target_link_libraries(evpp_unittest CloudEngine ${DEPENDENT_LIBRARIES})

if(HTTPS)
    file(GLOB evpp_https_unittest_SRCS https_client_test.cc
         ${PROJECT_SOURCE_DIR}/thirdparty/gtest/src/gtest-all.cc
         ${PROJECT_SOURCE_DIR}/thirdparty/gtest/src/gtest_main.cc)
    add_executable(evpp_https_unittest ${evpp_https_unittest_SRCS})
    set_target_properties(evpp_https_unittest PROPERTIES FOLDER "tests")
    target_link_libraries(evpp_https_unittest CloudEngine ${DEPENDENT_LIBRARIES})
endif()
