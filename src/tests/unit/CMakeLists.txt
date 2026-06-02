# ═══════════════════════════════════════════════════════════════════════════
# Unit tests — component isolation, no real I/O
# ═══════════════════════════════════════════════════════════════════════════

set(UNIT_LIBS Catch2::Catch2WithMain)

# ── Macro to simplify test target creation ──────────────────────────
macro(add_unit_test name)
    add_executable(${name} ${ARGN})
    configure_embedded_runtime_test_target(${name} ENGINE_TEST_SUITE_UNIT)
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

# ── Game business config tests ────────────────────────────────────────
add_unit_test(test_game_config config/test_game_config.cpp)
add_test(NAME unit.game_config COMMAND test_game_config)
set_tests_properties(unit.game_config PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

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

# ── VM coverage tests (comprehensive gap-filling) ──────────────────────
add_unit_test(test_vm_coverage vm/test_vm_coverage.cpp)
add_test(NAME unit.vm_coverage COMMAND test_vm_coverage)
set_tests_properties(unit.vm_coverage PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

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

add_unit_test(test_evpp_utilities network/test_evpp_utilities.cpp)
add_test(NAME unit.network.evpp_utilities COMMAND test_evpp_utilities)
set_tests_properties(unit.network.evpp_utilities PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_pid_file core/test_pid_file.cpp)
add_test(NAME unit.core.pid_file COMMAND test_pid_file)
set_tests_properties(unit.core.pid_file PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Entity tests ────────────────────────────────────────────────────
add_unit_test(test_entity entity/test_entity.cpp)
add_test(NAME unit.entity COMMAND test_entity)
set_tests_properties(unit.entity PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── RPC tests ───────────────────────────────────────────────────────
add_unit_test(test_rpc rpc/test_rpc.cpp)
add_test(NAME unit.rpc COMMAND test_rpc)
set_tests_properties(unit.rpc PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_rpc_bind script/test_rpc_bind.cpp)
add_test(NAME unit.rpc_bind COMMAND test_rpc_bind)
set_tests_properties(unit.rpc_bind PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_json_bind script/test_json_bind.cpp)
add_test(NAME unit.json_bind COMMAND test_json_bind)
set_tests_properties(unit.json_bind PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_net_bind script/test_net_bind.cpp)
add_test(NAME unit.net_bind COMMAND test_net_bind)
set_tests_properties(unit.net_bind PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_mem_bind script/test_mem_bind.cpp)
add_test(NAME unit.mem_bind COMMAND test_mem_bind)
set_tests_properties(unit.mem_bind PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_script_bind script/test_script_bind.cpp)
add_test(NAME unit.script_bind COMMAND test_script_bind)
set_tests_properties(unit.script_bind PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Auth tests ──────────────────────────────────────────────────────
add_unit_test(test_auth auth/test_auth.cpp)
add_test(NAME unit.auth COMMAND test_auth)
set_tests_properties(unit.auth PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Hot-reload tests ────────────────────────────────────────────────
add_unit_test(test_hotreload hotreload/test_hotreload.cpp)
add_test(NAME unit.hotreload COMMAND test_hotreload)
set_tests_properties(unit.hotreload PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Database ORM tests (only if MongoDB is enabled) ─────────────────
if(ENGINE_MONGODB_ENABLED)
    add_unit_test(test_data_service database/test_data_service.cpp)
    add_test(NAME unit.database.data_service COMMAND test_data_service)
    set_tests_properties(unit.database.data_service PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

    add_unit_test(test_orm database/test_orm.cpp)
    add_test(NAME unit.database.orm COMMAND test_orm)
    set_tests_properties(unit.database.orm PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")
endif()

# ── Database cache tests ────────────────────────────────────────────
add_unit_test(test_cache database/test_cache.cpp)
add_test(NAME unit.database.cache COMMAND test_cache)
set_tests_properties(unit.database.cache PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Monitoring tests ─────────────────────────────────────────────────
add_unit_test(test_metrics monitoring/test_metrics.cpp)
add_test(NAME unit.metrics COMMAND test_metrics)
set_tests_properties(unit.metrics PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_unit_test(test_health monitoring/test_health.cpp)
add_test(NAME unit.health COMMAND test_health)
set_tests_properties(unit.health PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# Profiler tests
add_unit_test(test_profiler profiler/test_profiler.cpp)
add_test(NAME unit.profiler COMMAND test_profiler)
set_tests_properties(unit.profiler PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── AOI tests ────────────────────────────────────────────────────────
add_unit_test(test_aoi aoi/test_aoi.cpp)
add_test(NAME unit.aoi COMMAND test_aoi)
set_tests_properties(unit.aoi PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Space tests ──────────────────────────────────────────────────────
add_unit_test(test_space space/test_space.cpp)
add_test(NAME unit.space COMMAND test_space)
set_tests_properties(unit.space PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Coroutine tests ──────────────────────────────────────────────────
add_unit_test(test_coroutine coroutine/test_coroutine.cpp)
add_test(NAME unit.coroutine COMMAND test_coroutine)
set_tests_properties(unit.coroutine PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Client API tests ────────────────────────────────────────────────
add_executable(test_client_api client_api/test_client_api.cpp)
configure_embedded_client_api_test_target(test_client_api ENGINE_TEST_SUITE_UNIT)
target_link_libraries(test_client_api PRIVATE Catch2::Catch2WithMain)
set_target_properties(test_client_api PROPERTIES FOLDER "tests/unit")
add_test(NAME unit.client_api COMMAND test_client_api)
set_tests_properties(unit.client_api PROPERTIES LABELS "unit" WORKING_DIRECTORY "${TEST_WORKING_DIR}")
