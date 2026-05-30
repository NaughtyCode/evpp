# ═══════════════════════════════════════════════════════════════════════════
# Lua script tests — run inside the engine via lua_test_runner
# ═══════════════════════════════════════════════════════════════════════════

# Lua test runner executable
add_executable(lua_test_runner lua_runner/lua_test_runner.cpp)
configure_embedded_runtime_test_target(lua_test_runner ENGINE_TEST_SUITE_LUA)
set_target_properties(lua_test_runner PROPERTIES FOLDER "tests/lua")

# Register Lua test files with CTest.
# The runner scans src/tests/lua/ subdirectories for test_*.lua files.

# Each Lua test file is a separate CTest test.
set(LUA_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ── Msgpack tests ───────────────────────────────────────────────────
add_test(NAME lua.msgpack
    COMMAND lua_test_runner "${LUA_TEST_DIR}/msgpack/test_msgpack.lua"
)
set_tests_properties(lua.msgpack PROPERTIES
    LABELS "lua" TIMEOUT 30
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

# ── Net tests ───────────────────────────────────────────────────────
add_test(NAME lua.net.client_server
    COMMAND lua_test_runner "${LUA_TEST_DIR}/net/test_client_server.lua"
)
set_tests_properties(lua.net.client_server PROPERTIES
    LABELS "lua;network"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

add_test(NAME lua.net.udp
    COMMAND lua_test_runner "${LUA_TEST_DIR}/net/test_udp.lua"
)
set_tests_properties(lua.net.udp PROPERTIES
    LABELS "lua;network" TIMEOUT 60
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

add_test(NAME lua.net.kcp
    COMMAND lua_test_runner "${LUA_TEST_DIR}/net/test_kcp.lua"
)
set_tests_properties(lua.net.kcp PROPERTIES
    LABELS "lua;network" TIMEOUT 60
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

# ── Timer tests ─────────────────────────────────────────────────────
add_test(NAME lua.timer.once
    COMMAND lua_test_runner "${LUA_TEST_DIR}/timer/test_timer_once.lua"
)
set_tests_properties(lua.timer.once PROPERTIES
    LABELS "lua" TIMEOUT 30
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

add_test(NAME lua.timer.interval
    COMMAND lua_test_runner "${LUA_TEST_DIR}/timer/test_timer_interval.lua"
)
set_tests_properties(lua.timer.interval PROPERTIES
    LABELS "lua" TIMEOUT 30
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

# ── Log tests ───────────────────────────────────────────────────────
add_test(NAME lua.log
    COMMAND lua_test_runner "${LUA_TEST_DIR}/log/test_log.lua"
)
set_tests_properties(lua.log PROPERTIES
    LABELS "lua" TIMEOUT 15
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

# ── Import tests ────────────────────────────────────────────────────
add_test(NAME lua.import
    COMMAND lua_test_runner
      "${LUA_TEST_DIR}/harness/test_harness.lua"
      "${LUA_TEST_DIR}/import/test_import.lua"
)
set_tests_properties(lua.import PROPERTIES
    LABELS "lua" TIMEOUT 15
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)

# ── Lua smoke test (also tagged "smoke") ────────────────────────────
add_test(NAME smoke.lua
    COMMAND lua_test_runner "${CMAKE_CURRENT_SOURCE_DIR}/../smoke/smoke_lua.lua"
)
set_tests_properties(smoke.lua PROPERTIES
    LABELS "smoke;lua" TIMEOUT 15
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
)
