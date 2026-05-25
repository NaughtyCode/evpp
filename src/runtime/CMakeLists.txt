cmake_minimum_required(VERSION 4.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENGINE_BUILD triggers dllexport in engine_api.h.
# Consumers who link against this library do NOT define this
# and will get dllimport on Windows / no-op elsewhere.
add_definitions(-DENGINE_BUILD)

option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" ON)

# ── Global include directories ───────────────────────────────────────
# All project includes are src/-relative.
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/lua/lua)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/glaze/glaze/include)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/concurrentqueue/concurrentqueue)
if(TARGET bson_static)
    include_directories(${PROJECT_SOURCE_DIR}/thirdparty/mongo-c-driver/src/libbson/src)
    include_directories(${PROJECT_BINARY_DIR}/mongo-c-driver/src/libbson/src)
endif()
if(TARGET mongoc_static)
    include_directories(${PROJECT_SOURCE_DIR}/thirdparty/mongo-c-driver/src/libmongoc/src)
    include_directories(${PROJECT_BINARY_DIR}/mongo-c-driver/src/libmongoc/src)
endif()

# ── Platform settings ────────────────────────────────────────────────
if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_compile_options(/bigobj /Zc:preprocessor /utf-8)
    # Moved from windows_port.h — suppress noisy / unactionable MSVC warnings.
    add_compile_options(
        /wd4005 /wd4244 /wd4251 /wd4355 /wd4503 /wd4530
        /wd4577 /wd4715 /wd4800 /wd4819 /wd4996
    )
endif()

if(UNIX)
    set(LIBRARIES event pthread)
else()
    set(LIBRARIES event_core_static event_extra_static)
endif()

# ═══════════════════════════════════════════════════════════════════════
# Third-party amalgamated sources
# ═══════════════════════════════════════════════════════════════════════

# ── Lua ────────────────────────────────────────────────────────────
set(LUA_SOURCES
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua/onelua.c
)
set_source_files_properties(${LUA_SOURCES} PROPERTIES COMPILE_DEFINITIONS "MAKE_LIB")
source_group("thirdparty\\lua" FILES ${LUA_SOURCES})

# ── Perfetto ───────────────────────────────────────────────────────
if(ENGINE_PROFILER_ENABLED)
    set(PERFETTO_SOURCES
        ${PROJECT_SOURCE_DIR}/thirdparty/perfetto/perfetto.cc
    )
    source_group("thirdparty\\perfetto" FILES ${PERFETTO_SOURCES})
else()
    set(PERFETTO_SOURCES "")
endif()

# ── KCP ──────────────────────────────────────────────────────────
set(KCP_SOURCES
    ${PROJECT_SOURCE_DIR}/thirdparty/kcp/ikcp.c
)
source_group("thirdparty\\kcp" FILES ${KCP_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# Engine modules
# ═══════════════════════════════════════════════════════════════════════

set(EXPORT_HEADER core/engine_api.h)
source_group("core" FILES ${EXPORT_HEADER})

# ── Log ────────────────────────────────────────────────────────────
set(LOG_SOURCES
    core/log/log.cc core/log/log.h
    core/log/log_levels.h core/log/log_context.h core/log/log_macros.h
)
source_group("core\\log" FILES ${LOG_SOURCES})

# ── Timer ──────────────────────────────────────────────────────────
set(TIMER_SOURCES
    core/timer/timer_manager.cc core/timer/timer_manager.h
    core/timer/timer.h core/timer/timer_core.h
    core/timer/timer_queue.h core/timer/clock_source.h
    core/timer/hr_timer.h core/timer/timer_wheel.h core/timer/alarm_timer.h
)
source_group("core\\timer" FILES ${TIMER_SOURCES})

# ── Config ─────────────────────────────────────────────────────────
set(CONFIG_SOURCES config/config.cc config/config.h)
source_group("config" FILES ${CONFIG_SOURCES})

# ── Engine core ────────────────────────────────────────────────────
set(ENGINE_SOURCES engine/engine.cc engine/engine.h)
source_group("engine" FILES ${ENGINE_SOURCES})

# ── Script VM ─────────────────────────────────────────────────────
set(VM_SOURCES
    vm/vm.cc vm/vm.h
    vm/script_importer.cc vm/script_importer.h
    vm/custom_ptr_store.cc vm/custom_ptr_store.h
)
source_group("vm" FILES ${VM_SOURCES})

# ── Script bindings ────────────────────────────────────────────────
set(SCRIPT_SOURCES
    script/script_bind.cc script/script_bind.h
    script/log_bind.cc script/log_bind.h
    script/timer_bind.cc script/timer_bind.h
    script/net_bind.cc script/net_bind.h
    script/net_client_bind.cc script/net_client_bind.h
    script/net_server_bind.cc script/net_server_bind.h
    script/net_http_bind.cc script/net_http_bind.h
    script/net_udp_client_bind.cc script/net_udp_client_bind.h
    script/net_udp_server_bind.cc script/net_udp_server_bind.h
    script/net_kcp_client_bind.cc script/net_kcp_client_bind.h
    script/net_kcp_server_bind.cc script/net_kcp_server_bind.h
    script/msgpack_bind.cc script/msgpack_bind.h
    script/import_bind.cc script/import_bind.h
)
source_group("script" FILES ${SCRIPT_SOURCES})

# ── Profiler ───────────────────────────────────────────────────────
set(PROFILER_SOURCES
    profiler/profiler_categories.h profiler/profiler_core.cc profiler/profiler_core.h
    profiler/profiler_events.h profiler/profiler_macros.h
)
source_group("profiler" FILES ${PROFILER_SOURCES})

# ── Physics ────────────────────────────────────────────────────────
set(PHYSICS_HEADERS
    physics/physics_config.h physics/physics_commands.h physics/physics_layers.h
    physics/physics_materials.h physics/physics_diff.h physics/physics_assets.h
    physics/physics_world.h physics/physics_thread.h physics/physics_system.h
    physics/physics_engine_bridge.h physics/physics_bindings.h
    physics/physics_vm.h physics/physics_log.h
)
source_group("physics" FILES ${PHYSICS_HEADERS})

set(PHYSICS_CC
    physics/physics_config.cc physics/physics_commands.cc
    physics/physics_layers.cc physics/physics_materials.cc
    physics/physics_diff.cc physics/physics_assets.cc
    physics/physics_world.cc physics/physics_thread.cc
    physics/physics_system.cc physics/physics_engine_bridge.cc
    physics/physics_bindings.cc physics/physics_vm.cc
)
source_group("physics" FILES ${PHYSICS_CC})

# ── Evpp network library (inlined) ─────────────────────────────────
file(GLOB EVPP_SOURCES
    evpp/*.cc evpp/*.h
    evpp/evpphttp/*.cc evpp/evpphttp/*.h
    evpp/http/*.cc evpp/http/*.h
    evpp/httpc/*.cc evpp/httpc/*.h
    evpp/udp/*.cc evpp/udp/*.h
    evpp/kcp/*.cc evpp/kcp/*.h
)
source_group("evpp" FILES ${EVPP_SOURCES})

# ── Mongo wrapper (database/mongo/) ───────────────────────────────────
set(MONGO_SOURCES
    database/mongo/mongo_bson.cc database/mongo/mongo_bson.h
    database/mongo/mongo_bulk.cc database/mongo/mongo_bulk.h
    database/mongo/mongo_client.cc database/mongo/mongo_client.h
    database/mongo/mongo_cursor.cc database/mongo/mongo_cursor.h
    database/mongo/mongo_error.cc database/mongo/mongo_error.h
    database/mongo/mongo_forward.h
    database/mongo/mongo_oid.cc database/mongo/mongo_oid.h
    database/mongo/mongo_session.cc database/mongo/mongo_session.h
    database/mongo/mongo_settings.cc database/mongo/mongo_settings.h
    database/mongo/mongo_system.cc database/mongo/mongo_system.h
    database/mongo/mongo_uri.cc database/mongo/mongo_uri.h
)
source_group("database\\mongo" FILES ${MONGO_SOURCES})

# ── Mongo Lua bindings (database/mongo_bind/) ─────────────────────────
set(MONGO_BIND_SOURCES
    database/mongo_bind/mongo_bind.cc database/mongo_bind/mongo_bind.h
)
source_group("database\\mongo_bind" FILES ${MONGO_BIND_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# ServerEngine library
# ═══════════════════════════════════════════════════════════════════════

# Physics sources are always visible as headers; .cc files are compiled
# only when ENGINE_PHYSICS_ENABLED is ON.  When OFF the bridge header
# provides inline empty stubs.
set(SERVERENGINE_SOURCES
    ${LUA_SOURCES}
    ${PERFETTO_SOURCES}
    ${KCP_SOURCES}
    ${EXPORT_HEADER}
    ${LOG_SOURCES}
    ${TIMER_SOURCES}
    ${CONFIG_SOURCES}
    ${ENGINE_SOURCES}
    ${VM_SOURCES}
    ${SCRIPT_SOURCES}
    ${PROFILER_SOURCES}
    ${PHYSICS_HEADERS}
    ${EVPP_SOURCES}
    ${MONGO_SOURCES}
    ${MONGO_BIND_SOURCES}
)

if(ENGINE_PHYSICS_ENABLED)
    list(APPEND SERVERENGINE_SOURCES ${PHYSICS_CC})
endif()

add_library(ServerEngine SHARED ${SERVERENGINE_SOURCES})

target_include_directories(ServerEngine PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua
)

# ── Compile definitions ───────────────────────────────────────────────
if(ENGINE_PHYSICS_ENABLED)
    target_compile_definitions(ServerEngine PUBLIC ENGINE_PHYSICS_ENABLED)
endif()

if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(ServerEngine PUBLIC ENGINE_PROFILER_ENABLED)
endif()

# ── JoltPhysics ───────────────────────────────────────────────────────
if(ENGINE_PHYSICS_ENABLED)
    # JoltPhysics v5.5.1 (https://github.com/jrouwe/JoltPhysics, MIT License)
    # Jolt.cmake sets CMAKE_CXX_STANDARD to 17 globally — restore C++23.
    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/JoltPhysics/JoltPhysics/Build jolt)
    set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    target_include_directories(ServerEngine PUBLIC
        ${PROJECT_SOURCE_DIR}/thirdparty/JoltPhysics/JoltPhysics
    )
    target_link_libraries(ServerEngine PUBLIC Jolt)
endif()

# ── Link mongo-c-driver ─────────────────────────────────────────────────
if(TARGET bson_static)
    target_link_libraries(ServerEngine PUBLIC bson_static)
endif()
if(TARGET mongoc_static)
    target_link_libraries(ServerEngine PUBLIC mongoc_static)
    if(WIN32)
        target_link_libraries(ServerEngine PUBLIC secur32 crypt32 Bcrypt ncrypt Dnsapi)
    endif()
endif()

# ── Link system libraries ─────────────────────────────────────────────
target_link_directories(ServerEngine PUBLIC "${PROJECT_BINARY_DIR}/libevent/lib/$<CONFIG>")
target_link_libraries(ServerEngine PUBLIC ${LIBRARIES})

# ── Post-build: copy DLL to bin/ ──────────────────────────────────────
add_custom_command(TARGET ServerEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:ServerEngine>"
    "${PROJECT_SOURCE_DIR}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:ServerEngine>"
    COMMENT "Copying ServerEngine.dll to bin/"
)
