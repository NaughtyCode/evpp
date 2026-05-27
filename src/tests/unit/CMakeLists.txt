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

# ── Engine tests ────────────────────────────────────────────────────
add_unit_test(test_engine engine/test_engine.cpp)
add_test(NAME unit.engine COMMAND test_engine)
set_tests_properties(unit.engine PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

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
