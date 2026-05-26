cmake_minimum_required(VERSION 4.0)

project(CloudEngine C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENGINE_ROOT always points to the shared src/ directory regardless of whether
# this file is the top-level project or included via add_subdirectory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# ENGINE_BUILD triggers dllexport in engine_api.h.
# Consumers who link against this library do NOT define this
# and will get dllimport on Windows / no-op elsewhere.
add_definitions(-DENGINE_BUILD)

option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" ON)
option(ENGINE_MONGODB_ENABLED "Enable MongoDB driver support" ON)

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

# ── Output paths ──────────────────────────────────────────────────────
set(LIBRARY_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/lib)
set(CMAKE_MACOSX_RPATH 0)

# ── Status ────────────────────────────────────────────────────────────
message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

# ═══════════════════════════════════════════════════════════════════════
# Third-party dependencies
# ═══════════════════════════════════════════════════════════════════════

# ── libevent (built from source) ──────────────────────────────────────
set(EVENT__DISABLE_TESTS ON CACHE BOOL "Disable libevent tests")
set(EVENT__DISABLE_REGRESS ON CACHE BOOL "Disable libevent regression tests")
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "Disable libevent samples")
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "Disable libevent benchmarks")
set(EVENT__DISABLE_OPENSSL ON CACHE STRING "Disable OpenSSL support")
set(EVENT__DISABLE_MBEDTLS ON CACHE STRING "Disable MbedTLS support")
set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "Build libevent as static library")
set(EVENT__HAVE_WEPOLL OFF CACHE BOOL "Disable wepoll backend")
add_subdirectory(${ENGINE_ROOT}/thirdparty/libevent ${CMAKE_BINARY_DIR}/libevent)
include_directories(${CMAKE_BINARY_DIR}/libevent/include)
set_property(TARGET event_core_static event_extra_static event_static PROPERTY FOLDER "thirdparty")
set_property(TARGET event_core event_extra event PROPERTY FOLDER "thirdparty")

# ── mongo-c-driver (static, bundled deps) ─────────────────────────────
if(ENGINE_MONGODB_ENABLED)
    set(ENABLE_MONGOC ON CACHE BOOL "" FORCE)
    set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
    set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZLIB BUNDLED CACHE STRING "" FORCE)
    set(ENABLE_ZSTD OFF CACHE STRING "" FORCE)
    set(ENABLE_SNAPPY OFF CACHE STRING "" FORCE)
    set(ENABLE_SRV ON CACHE BOOL "" FORCE)
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENABLE_CLIENT_SIDE_ENCRYPTION OFF CACHE BOOL "" FORCE)
    set(ENABLE_MONGODB_AWS_AUTH OFF CACHE BOOL "" FORCE)
    set(ENABLE_UNINSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_MAN_PAGES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HTML_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TRACING OFF CACHE BOOL "" FORCE)
    if (WIN32)
        set(ENABLE_SSL WINDOWS CACHE STRING "" FORCE)
        set(ENABLE_SASL SSPI CACHE STRING "" FORCE)
    else ()
        set(ENABLE_SSL AUTO CACHE STRING "" FORCE)
        set(ENABLE_SASL AUTO CACHE STRING "" FORCE)
    endif ()
    add_subdirectory(${ENGINE_ROOT}/thirdparty/mongo-c-driver
                     ${CMAKE_BINARY_DIR}/mongo-c-driver)
    # mongo-c-driver sources are UTF-8; MSVC needs this to parse string literals
    if(MSVC)
        target_compile_options(bson_static PRIVATE /utf-8)
        if(TARGET zlib_obj)
            target_compile_options(zlib_obj PRIVATE /utf-8)
        endif()
        if(TARGET utf8proc_obj)
            target_compile_options(utf8proc_obj PRIVATE /utf-8)
        endif()
        if(TARGET mongoc_static)
            target_compile_options(mongoc_static PRIVATE /utf-8)
        endif()
    endif()
    set_property(TARGET bson_static PROPERTY FOLDER "thirdparty")
    if(TARGET zlib_obj)
        set_property(TARGET zlib_obj PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET utf8proc_obj)
        set_property(TARGET utf8proc_obj PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET mongoc_static)
        set_property(TARGET mongoc_static PROPERTY FOLDER "thirdparty")
    endif()
    if(TARGET kms_message_static)
        set_property(TARGET kms_message_static PROPERTY FOLDER "thirdparty")
    endif()
endif()

# ── Global include directories ───────────────────────────────────────
# All project includes are src/-relative.
include_directories(${ENGINE_ROOT})
include_directories(${ENGINE_ROOT}/thirdparty/libevent/include)
include_directories(${ENGINE_ROOT}/thirdparty/quill/quill/include)
include_directories(${ENGINE_ROOT}/thirdparty/lua/src)
include_directories(${ENGINE_ROOT}/thirdparty/glaze/glaze/include)
include_directories(${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue)
if(TARGET bson_static)
    include_directories(${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libbson/src)
    include_directories(${CMAKE_BINARY_DIR}/mongo-c-driver/src/libbson/src)
endif()
if(TARGET mongoc_static)
    include_directories(${ENGINE_ROOT}/thirdparty/mongo-c-driver/src/libmongoc/src)
    include_directories(${CMAKE_BINARY_DIR}/mongo-c-driver/src/libmongoc/src)
endif()

# ═══════════════════════════════════════════════════════════════════════
# Third-party sources
# ═══════════════════════════════════════════════════════════════════════

# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
# Core VM (each #defines LUA_CORE)
set(LUA_CORE_SOURCES
    ${ENGINE_ROOT}/thirdparty/lua/src/lapi.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lcode.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lctype.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldebug.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldo.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldump.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lfunc.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lgc.c
    ${ENGINE_ROOT}/thirdparty/lua/src/llex.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lmem.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lobject.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lopcodes.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lparser.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstate.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstring.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltable.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltm.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lundump.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lvm.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lzio.c
    # evpp custom: dynamic void* array attached to global_State
    ${ENGINE_ROOT}/thirdparty/lua/src/lstate_custom_ptr.c
)

# Standard libraries (each #defines LUA_LIB)
set(LUA_LIB_SOURCES
    ${ENGINE_ROOT}/thirdparty/lua/src/lauxlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lbaselib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lcorolib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ldblib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/liolib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lmathlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/loadlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/loslib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lstrlib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/ltablib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/lutf8lib.c
    ${ENGINE_ROOT}/thirdparty/lua/src/linit.c
)

set(LUA_SOURCES ${LUA_CORE_SOURCES} ${LUA_LIB_SOURCES})
source_group("thirdparty\\lua" FILES ${LUA_SOURCES})

# ── Perfetto ───────────────────────────────────────────────────────
if(ENGINE_PROFILER_ENABLED)
    set(PERFETTO_SOURCES
        ${ENGINE_ROOT}/thirdparty/perfetto/perfetto.cc
    )
    source_group("thirdparty\\perfetto" FILES ${PERFETTO_SOURCES})
else()
    set(PERFETTO_SOURCES "")
endif()

# ── KCP ──────────────────────────────────────────────────────────
set(KCP_SOURCES
    ${ENGINE_ROOT}/thirdparty/kcp/ikcp.c
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
    database/mongo/mongo_apm.cc database/mongo/mongo_apm.h
    database/mongo/mongo_bson.cc database/mongo/mongo_bson.h
    database/mongo/mongo_bson_ext.cc database/mongo/mongo_bson_ext.h
    database/mongo/mongo_bson_vector.cc database/mongo/mongo_bson_vector.h
    database/mongo/mongo_bulk.cc database/mongo/mongo_bulk.h
    database/mongo/mongo_bulkwrite.cc database/mongo/mongo_bulkwrite.h
    database/mongo/mongo_change_stream.cc database/mongo/mongo_change_stream.h
    database/mongo/mongo_client.cc database/mongo/mongo_client.h
    database/mongo/mongo_client_pool.cc database/mongo/mongo_client_pool.h
    database/mongo/mongo_cursor.cc database/mongo/mongo_cursor.h
    database/mongo/mongo_encryption.cc database/mongo/mongo_encryption.h
    database/mongo/mongo_error.cc database/mongo/mongo_error.h
    database/mongo/mongo_find_and_modify_opts.cc database/mongo/mongo_find_and_modify_opts.h
    database/mongo/mongo_forward.h
    database/mongo/mongo_gridfs.cc database/mongo/mongo_gridfs.h
    database/mongo/mongo_handshake.cc database/mongo/mongo_handshake.h
    database/mongo/mongo_host_list.cc database/mongo/mongo_host_list.h
    database/mongo/mongo_index_model.cc database/mongo/mongo_index_model.h
    database/mongo/mongo_init.cc database/mongo/mongo_init.h
    database/mongo/mongo_log.cc database/mongo/mongo_log.h
    database/mongo/mongo_oid.cc database/mongo/mongo_oid.h
    database/mongo/mongo_oidc.cc database/mongo/mongo_oidc.h
    database/mongo/mongo_optional.cc database/mongo/mongo_optional.h
    database/mongo/mongo_rand.cc database/mongo/mongo_rand.h
    database/mongo/mongo_server_api.cc database/mongo/mongo_server_api.h
    database/mongo/mongo_session.cc database/mongo/mongo_session.h
    database/mongo/mongo_settings.cc database/mongo/mongo_settings.h
    database/mongo/mongo_socket.cc database/mongo/mongo_socket.h
    database/mongo/mongo_ssl.cc database/mongo/mongo_ssl.h
    database/mongo/mongo_stream.cc database/mongo/mongo_stream.h
    database/mongo/mongo_system.cc database/mongo/mongo_system.h
    database/mongo/mongo_topology.cc database/mongo/mongo_topology.h
    database/mongo/mongo_uri.cc database/mongo/mongo_uri.h
    database/mongo/mongo_version.cc database/mongo/mongo_version.h
)
source_group("database\\mongo" FILES ${MONGO_SOURCES})

# ── Mongo Lua bindings (database/mongo_bind/) ─────────────────────────
set(MONGO_BIND_SOURCES
    database/mongo_bind/mongo_bind.cc database/mongo_bind/mongo_bind.h
)
source_group("database\\mongo_bind" FILES ${MONGO_BIND_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# CloudEngine library
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

add_library(CloudEngine SHARED ${SERVERENGINE_SOURCES})

target_include_directories(CloudEngine PUBLIC
    ${ENGINE_ROOT}
    ${ENGINE_ROOT}/thirdparty/libevent/include
    ${CMAKE_BINARY_DIR}/libevent/include
    ${ENGINE_ROOT}/thirdparty/quill/quill/include
    ${ENGINE_ROOT}/thirdparty/lua/src
    ${ENGINE_ROOT}/thirdparty/glaze/glaze/include
    ${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue
)

# ── Compile definitions ───────────────────────────────────────────────
if(ENGINE_PHYSICS_ENABLED)
    target_compile_definitions(CloudEngine PUBLIC ENGINE_PHYSICS_ENABLED)
endif()

if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(CloudEngine PUBLIC ENGINE_PROFILER_ENABLED)
endif()

# ── JoltPhysics ───────────────────────────────────────────────────────
if(ENGINE_PHYSICS_ENABLED)
    # JoltPhysics v5.5.1 (https://github.com/jrouwe/JoltPhysics, MIT License)
    # Jolt.cmake sets CMAKE_CXX_STANDARD to 17 globally — restore C++23.
    add_subdirectory(${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics/Build jolt)
    set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    target_include_directories(CloudEngine PUBLIC
        ${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics
    )
    target_link_libraries(CloudEngine PUBLIC Jolt)
endif()

# ── Link mongo-c-driver ─────────────────────────────────────────────────
if(TARGET bson_static)
    target_link_libraries(CloudEngine PUBLIC bson_static)
endif()
if(TARGET mongoc_static)
    target_link_libraries(CloudEngine PUBLIC mongoc_static)
    if(WIN32)
        target_link_libraries(CloudEngine PUBLIC secur32 crypt32 Bcrypt ncrypt Dnsapi)
    endif()
endif()

# ── Link system libraries ─────────────────────────────────────────────
target_link_directories(CloudEngine PUBLIC "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
target_link_libraries(CloudEngine PUBLIC ${LIBRARIES})

# ── Post-build: copy DLL to bin/ ──────────────────────────────────────
add_custom_command(TARGET CloudEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:CloudEngine>"
    "${ENGINE_ROOT}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:CloudEngine>"
    COMMENT "Copying CloudEngine.dll to bin/"
)
