include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB evpp_unittest_SRCS CONFIGURE_DEPENDS *.cc
     ${ENGINE_ROOT}/thirdparty/gtest/src/gtest-all.cc
     ${ENGINE_ROOT}/thirdparty/gtest/src/gtest_main.cc)

list(REMOVE_ITEM evpp_unittest_SRCS
     ${CMAKE_CURRENT_SOURCE_DIR}/https_client_test.cc
     ${CMAKE_CURRENT_SOURCE_DIR}/glog_test.cc)

add_executable(evpp_unittest ${evpp_unittest_SRCS})
set_target_properties(evpp_unittest PROPERTIES FOLDER "tests")
target_link_libraries(evpp_unittest CloudEngine)

if(HTTPS)
    file(GLOB evpp_https_unittest_SRCS CONFIGURE_DEPENDS https_client_test.cc
         ${ENGINE_ROOT}/thirdparty/gtest/src/gtest-all.cc
         ${ENGINE_ROOT}/thirdparty/gtest/src/gtest_main.cc)
    add_executable(evpp_https_unittest ${evpp_https_unittest_SRCS})
    set_target_properties(evpp_https_unittest PROPERTIES FOLDER "tests")
    target_link_libraries(evpp_https_unittest CloudEngine)
endif()
