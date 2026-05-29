# ═══════════════════════════════════════════════════════════════════════════
# Integration tests — multi-component, real socket I/O
# ═══════════════════════════════════════════════════════════════════════════

set(INTEG_LIBS Catch2::Catch2WithMain CloudEngine)

macro(add_integ_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_COMMON_INCLUDES})
    target_compile_definitions(${name} PRIVATE "CLOUD_ENGINE_API=")
    target_link_libraries(${name} PRIVATE ${INTEG_LIBS})
    set_target_properties(${name} PROPERTIES FOLDER "tests/integration")
endmacro()

# ── Network ──────────────────────────────────────────────────────────
add_integ_test(test_integ_tcp network/test_tcp.cpp)
add_test(NAME integration.network.tcp COMMAND test_integ_tcp)
set_tests_properties(integration.network.tcp PROPERTIES
    TIMEOUT 120 LABELS "integration;network"
    RUN_SERIAL TRUE
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_integ_test(test_integ_udp network/test_udp.cpp)
add_test(NAME integration.network.udp COMMAND test_integ_udp)
set_tests_properties(integration.network.udp PROPERTIES
    TIMEOUT 60 LABELS "integration;network"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_integ_test(test_integ_conn_limit network/test_connection_limit.cpp)
add_test(NAME integration.network.conn_limit COMMAND test_integ_conn_limit)
set_tests_properties(integration.network.conn_limit PROPERTIES
    TIMEOUT 120 LABELS "integration;network"
    RUN_SERIAL TRUE
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Entity + Network integration ──────────────────────────────────────
add_integ_test(test_integ_entity_net entity/test_entity_net.cpp)
add_test(NAME integration.entity.net COMMAND test_integ_entity_net)
set_tests_properties(integration.entity.net PROPERTIES
    TIMEOUT 60 LABELS "integration;entity;net"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Entity + Auth integration ──────────────────────────────────────────
add_integ_test(test_integ_auth_entity entity/test_auth_entity.cpp)
add_test(NAME integration.auth.entity COMMAND test_integ_auth_entity)
set_tests_properties(integration.auth.entity PROPERTIES
    TIMEOUT 60 LABELS "integration;auth;entity"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Entity + ORM integration (only if MongoDB is enabled) ────────────
if(ENGINE_MONGODB_ENABLED)
    add_integ_test(test_integ_orm_entity entity/test_orm_entity.cpp)
    add_test(NAME integration.orm.entity COMMAND test_integ_orm_entity)
    set_tests_properties(integration.orm.entity PROPERTIES
        TIMEOUT 60 LABELS "integration;orm;entity"
        WORKING_DIRECTORY "${TEST_WORKING_DIR}")
endif()

# ── Entity + AOI integration ──────────────────────────────────────────
add_integ_test(test_integ_entity_aoi entity/test_entity_aoi.cpp)
add_test(NAME integration.entity.aoi COMMAND test_integ_entity_aoi)
set_tests_properties(integration.entity.aoi PROPERTIES
    TIMEOUT 60 LABELS "integration;entity;aoi"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Physics (only if engine was built with physics) ──────────────────
if(ENGINE_PHYSICS_ENABLED)
    add_integ_test(test_integ_physics physics/test_physics.cpp physics/test_physics_fetch_cv.cpp)
    add_test(NAME integration.physics COMMAND test_integ_physics)
    set_tests_properties(integration.physics PROPERTIES
        TIMEOUT 60 LABELS "integration;physics"
        RUN_SERIAL TRUE
        WORKING_DIRECTORY "${TEST_WORKING_DIR}")
endif()

# ── Space + Entity integration ──────────────────────────────────────
add_integ_test(test_integ_space_entity space/test_space_entity.cpp)
add_test(NAME integration.space.entity COMMAND test_integ_space_entity)
set_tests_properties(integration.space.entity PROPERTIES
    TIMEOUT 60 LABELS "integration;space;entity"
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# ── Client API (only if GameClient target exists) ────────────────────
if(TARGET GameClient)
    add_executable(test_integ_client_tcp client_api/test_client_tcp.cpp)
    target_include_directories(test_integ_client_tcp PRIVATE ${TEST_COMMON_INCLUDES})
    target_compile_definitions(test_integ_client_tcp PRIVATE "CLOUD_ENGINE_API=")
    target_link_libraries(test_integ_client_tcp PRIVATE Catch2::Catch2WithMain GameClient)
    set_target_properties(test_integ_client_tcp PROPERTIES FOLDER "tests/integration")
    add_test(NAME integration.client_api.tcp COMMAND test_integ_client_tcp)
endif()
