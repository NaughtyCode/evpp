include_directories(${ENGINE_ROOT} ${ENGINE_ROOT}/thirdparty)


set(LIBRARIES CloudEngine)

add_subdirectory(apps)
add_subdirectory(chatroom)
add_subdirectory(recipes)
add_subdirectory(http)
add_subdirectory(c10m)

add_executable(example_httpecho echo/httpecho/main.cc)
target_link_libraries(example_httpecho ${LIBRARIES})

add_executable(example_tcpecho echo/tcpecho/main.cc)
target_link_libraries(example_tcpecho ${LIBRARIES})

add_executable(example_udpecho echo/udpecho/main.cc)
target_link_libraries(example_udpecho ${LIBRARIES})

add_executable(example_pingpong_client pingpong/client.cc)
target_link_libraries(example_pingpong_client ${LIBRARIES})

add_executable(example_pingpong_server pingpong/server.cc)
target_link_libraries(example_pingpong_server ${LIBRARIES})

add_executable(example_tcp_client tcp/tcp_client.cc)
target_link_libraries(example_tcp_client ${LIBRARIES})

add_executable(example_tcp_server tcp/tcp_server.cc)
target_link_libraries(example_tcp_server ${LIBRARIES})

add_executable(example_multi_accept multi_accept/main.cc)
target_link_libraries(example_multi_accept ${LIBRARIES})

add_executable(example_twisted_pubsubserver twisted/pubsubserver/main.cc)
target_link_libraries(example_twisted_pubsubserver ${LIBRARIES})

add_executable(example_twisted_webserver twisted/webserver/main.cc)
target_link_libraries(example_twisted_webserver ${LIBRARIES})

add_executable(example_timer01 timer/timer01.cc)
target_link_libraries(example_timer01 ${LIBRARIES})

add_executable(example_timer02 timer/timer02.cc)
target_link_libraries(example_timer02 ${LIBRARIES})

add_executable(example_timer03 timer/timer03.cc)
target_link_libraries(example_timer03 ${LIBRARIES})

add_executable(example_timer04 timer/timer04.cc)
target_link_libraries(example_timer04 ${LIBRARIES})

add_executable(example_timer05 timer/timer05.cc)
target_link_libraries(example_timer05 ${LIBRARIES})

add_executable(example_dns_resolve dns_resolve/main.cc)
target_link_libraries(example_dns_resolve ${LIBRARIES})

add_executable(example_signal01 signal/signal01.cc)
target_link_libraries(example_signal01 ${LIBRARIES})

add_executable(example_signal02 signal/signal02.cc)
target_link_libraries(example_signal02 ${LIBRARIES})

set_target_properties(
    example_httpecho example_tcpecho example_udpecho
    example_pingpong_client example_pingpong_server
    example_tcp_client example_tcp_server
    example_multi_accept
    example_twisted_pubsubserver example_twisted_webserver
    example_timer01 example_timer02 example_timer03 example_timer04 example_timer05
    example_dns_resolve
    example_signal01 example_signal02
    PROPERTIES FOLDER "tests/examples")
