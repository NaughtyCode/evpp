add_test(NAME smoke.engine_init COMMAND test_smoke_engine)
set_tests_properties(smoke.engine_init PROPERTIES
    TIMEOUT 30 LABELS "smoke"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_test(NAME smoke.scriptvm   COMMAND test_smoke_scriptvm)
set_tests_properties(smoke.scriptvm PROPERTIES
    TIMEOUT 30 LABELS "smoke"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_test(NAME smoke.config_load COMMAND test_smoke_config)
set_tests_properties(smoke.config_load PROPERTIES
    TIMEOUT 30 LABELS "smoke"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_test(NAME smoke.tcp_loopback COMMAND test_smoke_tcp)
set_tests_properties(smoke.tcp_loopback PROPERTIES
    TIMEOUT 30 LABELS "smoke"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

set(SMOKE_LIBS Catch2::Catch2WithMain)

add_executable(test_smoke_engine smoke_engine_init.cpp)
configure_embedded_runtime_test_target(test_smoke_engine ENGINE_TEST_SUITE_SMOKE)
target_link_libraries(test_smoke_engine PRIVATE ${SMOKE_LIBS})
set_target_properties(test_smoke_engine PROPERTIES FOLDER "tests/smoke")

add_executable(test_smoke_scriptvm smoke_scriptvm.cpp)
configure_embedded_runtime_test_target(test_smoke_scriptvm ENGINE_TEST_SUITE_SMOKE)
target_link_libraries(test_smoke_scriptvm PRIVATE ${SMOKE_LIBS})
set_target_properties(test_smoke_scriptvm PROPERTIES FOLDER "tests/smoke")

add_executable(test_smoke_config smoke_config_load.cpp)
configure_embedded_runtime_test_target(test_smoke_config ENGINE_TEST_SUITE_SMOKE)
target_link_libraries(test_smoke_config PRIVATE ${SMOKE_LIBS})
set_target_properties(test_smoke_config PROPERTIES FOLDER "tests/smoke")

add_executable(test_smoke_tcp smoke_tcp_loopback.cpp)
configure_embedded_runtime_test_target(test_smoke_tcp ENGINE_TEST_SUITE_SMOKE)
target_link_libraries(test_smoke_tcp PRIVATE ${SMOKE_LIBS})
set_target_properties(test_smoke_tcp PROPERTIES FOLDER "tests/smoke")
