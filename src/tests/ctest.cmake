# CTest configuration for CloudEngine test suite.

# ── Global timeout (default 120s) ────────────────────────────────────
set(CTEST_TEST_TIMEOUT 120)

# ── Smoke tests: fail if > 30s ──────────────────────────────────────
set_tests_properties(
    smoke.engine_init
    smoke.scriptvm
    smoke.tcp_loopback
    smoke.config_load
    smoke.lua
    PROPERTIES TIMEOUT 30 LABELS "smoke"
)

# ── Unit tests: standard timeout ────────────────────────────────────
# Labels are set per-test in the CMakeLists.txt that creates them.

# ── Integration tests: extra time for network I/O ───────────────────
set_tests_properties(
    integration.network.tcp
    integration.network.udp
    integration.client_api.tcp
    PROPERTIES TIMEOUT 60
)

# ── Lua tests: startup + script execution time ──────────────────────
set_tests_properties(
    lua.net.client_server
    lua.net.udp
    lua.net.kcp
    PROPERTIES TIMEOUT 120
)

# ── Performance tests: no timeout ────────────────────────────────────
if(CLOUDENGINE_BUILD_BENCHMARKS)
    set_tests_properties(
        performance.bench_tcp
        PROPERTIES TIMEOUT 0 LABELS "performance"
    )
endif()
