
add_executable(example_chatroom_simple_server server.cc codec.h)
target_link_libraries(example_chatroom_simple_server ${LIBRARIES})

add_executable(example_chatroom_simple_client client.cc codec.h)
target_link_libraries(example_chatroom_simple_client ${LIBRARIES})

set_target_properties(example_chatroom_simple_server example_chatroom_simple_client PROPERTIES FOLDER "tests/examples")
