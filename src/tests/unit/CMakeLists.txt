# ═══════════════════════════════════════════════════════════════════════════
# Unit tests — component isolation, no real I/O
# ═══════════════════════════════════════════════════════════════════════════

set(UNIT_LIBS Catch2::Catch2WithMain CloudEngine)

# ── Macro to simplify test target creation ──────────────────────────
macro(add_unit_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_COMMON_INCLUDES})
    target_link_libraries(${name} PRIVATE ${UNIT_LIBS})
    set_target_properties(${name} PROPERTIES FOLDER "tests/unit")
endmacro()

# ── Buffer tests ────────────────────────────────────────────────────
add_unit_test(test_buffer buffer/test_buffer.cpp)
add_test(NAME unit.buffer COMMAND test_buffer)
set_tests_properties(unit.buffer PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Config tests ────────────────────────────────────────────────────
add_unit_test(test_config config/test_config.cpp)
add_test(NAME unit.config COMMAND test_config)
set_tests_properties(unit.config PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Timer tests ─────────────────────────────────────────────────────
add_unit_test(test_timer timer/test_timer.cpp)
add_test(NAME unit.timer COMMAND test_timer)
set_tests_properties(unit.timer PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── ScriptVM tests ──────────────────────────────────────────────────
add_unit_test(test_scriptvm vm/test_scriptvm.cpp)
add_test(NAME unit.scriptvm COMMAND test_scriptvm)
set_tests_properties(unit.scriptvm PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Sandbox tests ──────────────────────────────────────────────────────
add_unit_test(test_sandbox vm/test_sandbox.cpp)
add_test(NAME unit.sandbox COMMAND test_sandbox)
set_tests_properties(unit.sandbox PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── luaL_error safety tests ─────────────────────────────────────────────
add_unit_test(test_lual_error_safety vm/test_lual_error_safety.cpp)
add_test(NAME unit.lual_error_safety COMMAND test_lual_error_safety)
set_tests_properties(unit.lual_error_safety PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── RunInLoop safety tests ──────────────────────────────────────────────
add_unit_test(test_runinloop_safety vm/test_runinloop_safety.cpp)
add_test(NAME unit.runinloop_safety COMMAND test_runinloop_safety)
set_tests_properties(unit.runinloop_safety PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Message size limits tests ────────────────────────────────────────────
add_unit_test(test_message_limits vm/test_message_limits.cpp)
add_test(NAME unit.message_limits COMMAND test_message_limits)
set_tests_properties(unit.message_limits PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Engine tests ────────────────────────────────────────────────────
add_unit_test(test_engine engine/test_engine.cpp)
add_test(NAME unit.engine COMMAND test_engine)
set_tests_properties(unit.engine PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Network codec tests ──────────────────────────────────────────────
add_unit_test(test_length_prefixed_codec network/test_length_prefixed_codec.cpp)
add_test(NAME unit.network.length_prefixed_codec COMMAND test_length_prefixed_codec)
set_tests_properties(unit.network.length_prefixed_codec PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Rate limiter tests ───────────────────────────────────────────────
add_unit_test(test_rate_limiter network/test_rate_limiter.cpp)
add_test(NAME unit.network.rate_limiter COMMAND test_rate_limiter)
set_tests_properties(unit.network.rate_limiter PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Entity tests ────────────────────────────────────────────────────
add_unit_test(test_entity entity/test_entity.cpp)
add_test(NAME unit.entity COMMAND test_entity)
set_tests_properties(unit.entity PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Client API tests ────────────────────────────────────────────────
# Only if GameClient target exists
if(TARGET GameClient)
    add_executable(test_client_api client_api/test_client_api.cpp)
    target_include_directories(test_client_api PRIVATE ${TEST_COMMON_INCLUDES})
    target_link_libraries(test_client_api PRIVATE Catch2::Catch2WithMain GameClient)
    set_target_properties(test_client_api PROPERTIES FOLDER "tests/unit")
    add_test(NAME unit.client_api COMMAND test_client_api)
    set_tests_properties(unit.client_api PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")
endif()
