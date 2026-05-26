# Pure file-organization module — no compilation options, no targets.
# Include via include() to obtain source-file lists and source_group
# definitions for everything under src/runtime/.
#
# Provides:
#   SERVERENGINE_SOURCES  — combined source list for the CloudEngine library
#   Individual lists: LUA_SOURCES, LOG_SOURCES, TIMER_SOURCES, etc.

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# Absolute path to this (runtime/) source directory.
set(RUNTIME_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ═══════════════════════════════════════════════════════════════════════
# Third-party sources
# ═══════════════════════════════════════════════════════════════════════

# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
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
    ${ENGINE_ROOT}/thirdparty/lua/src/lstate_custom_ptr.c
)

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

set(EXPORT_HEADER ${RUNTIME_DIR}/core/engine_api.h)
source_group("core" FILES ${EXPORT_HEADER})

# ── Log ────────────────────────────────────────────────────────────
set(LOG_SOURCES
    ${RUNTIME_DIR}/core/log/log.cc ${RUNTIME_DIR}/core/log/log.h
    ${RUNTIME_DIR}/core/log/log_levels.h ${RUNTIME_DIR}/core/log/log_context.h ${RUNTIME_DIR}/core/log/log_macros.h
)
source_group("core\\log" FILES ${LOG_SOURCES})

# ── Timer ──────────────────────────────────────────────────────────
set(TIMER_SOURCES
    ${RUNTIME_DIR}/core/timer/timer_manager.cc ${RUNTIME_DIR}/core/timer/timer_manager.h
    ${RUNTIME_DIR}/core/timer/timer.h ${RUNTIME_DIR}/core/timer/timer_core.h
    ${RUNTIME_DIR}/core/timer/timer_queue.h ${RUNTIME_DIR}/core/timer/clock_source.h
    ${RUNTIME_DIR}/core/timer/hr_timer.h ${RUNTIME_DIR}/core/timer/timer_wheel.h ${RUNTIME_DIR}/core/timer/alarm_timer.h
)
source_group("core\\timer" FILES ${TIMER_SOURCES})

# ── Config ─────────────────────────────────────────────────────────
set(CONFIG_SOURCES ${RUNTIME_DIR}/config/config.cc ${RUNTIME_DIR}/config/config.h)
source_group("config" FILES ${CONFIG_SOURCES})

# ── Engine core ────────────────────────────────────────────────────
set(ENGINE_SOURCES ${RUNTIME_DIR}/engine/engine.cc ${RUNTIME_DIR}/engine/engine.h)
source_group("engine" FILES ${ENGINE_SOURCES})

# ── Script VM ─────────────────────────────────────────────────────
set(VM_SOURCES
    ${RUNTIME_DIR}/vm/vm.cc ${RUNTIME_DIR}/vm/vm.h
    ${RUNTIME_DIR}/vm/script_importer.cc ${RUNTIME_DIR}/vm/script_importer.h
    ${RUNTIME_DIR}/vm/custom_ptr_store.cc ${RUNTIME_DIR}/vm/custom_ptr_store.h
)
source_group("vm" FILES ${VM_SOURCES})

# ── Script bindings ────────────────────────────────────────────────
set(SCRIPT_SOURCES
    ${RUNTIME_DIR}/script/script_bind.cc ${RUNTIME_DIR}/script/script_bind.h
    ${RUNTIME_DIR}/script/log_bind.cc ${RUNTIME_DIR}/script/log_bind.h
    ${RUNTIME_DIR}/script/timer_bind.cc ${RUNTIME_DIR}/script/timer_bind.h
    ${RUNTIME_DIR}/script/net_bind.cc ${RUNTIME_DIR}/script/net_bind.h
    ${RUNTIME_DIR}/script/net_client_bind.cc ${RUNTIME_DIR}/script/net_client_bind.h
    ${RUNTIME_DIR}/script/net_server_bind.cc ${RUNTIME_DIR}/script/net_server_bind.h
    ${RUNTIME_DIR}/script/net_http_bind.cc ${RUNTIME_DIR}/script/net_http_bind.h
    ${RUNTIME_DIR}/script/net_udp_client_bind.cc ${RUNTIME_DIR}/script/net_udp_client_bind.h
    ${RUNTIME_DIR}/script/net_udp_server_bind.cc ${RUNTIME_DIR}/script/net_udp_server_bind.h
    ${RUNTIME_DIR}/script/net_kcp_client_bind.cc ${RUNTIME_DIR}/script/net_kcp_client_bind.h
    ${RUNTIME_DIR}/script/net_kcp_server_bind.cc ${RUNTIME_DIR}/script/net_kcp_server_bind.h
    ${RUNTIME_DIR}/script/msgpack_bind.cc ${RUNTIME_DIR}/script/msgpack_bind.h
    ${RUNTIME_DIR}/script/import_bind.cc ${RUNTIME_DIR}/script/import_bind.h
)
source_group("script" FILES ${SCRIPT_SOURCES})

# ── Profiler ───────────────────────────────────────────────────────
set(PROFILER_SOURCES
    ${RUNTIME_DIR}/profiler/profiler_categories.h ${RUNTIME_DIR}/profiler/profiler_core.cc ${RUNTIME_DIR}/profiler/profiler_core.h
    ${RUNTIME_DIR}/profiler/profiler_events.h ${RUNTIME_DIR}/profiler/profiler_macros.h
)
source_group("profiler" FILES ${PROFILER_SOURCES})

# ── Physics ────────────────────────────────────────────────────────
set(PHYSICS_HEADERS
    ${RUNTIME_DIR}/physics/physics_config.h ${RUNTIME_DIR}/physics/physics_commands.h ${RUNTIME_DIR}/physics/physics_layers.h
    ${RUNTIME_DIR}/physics/physics_materials.h ${RUNTIME_DIR}/physics/physics_diff.h ${RUNTIME_DIR}/physics/physics_assets.h
    ${RUNTIME_DIR}/physics/physics_world.h ${RUNTIME_DIR}/physics/physics_thread.h ${RUNTIME_DIR}/physics/physics_system.h
    ${RUNTIME_DIR}/physics/physics_engine_bridge.h ${RUNTIME_DIR}/physics/physics_bindings.h
    ${RUNTIME_DIR}/physics/physics_vm.h ${RUNTIME_DIR}/physics/physics_log.h
)
source_group("physics" FILES ${PHYSICS_HEADERS})

set(PHYSICS_CC
    ${RUNTIME_DIR}/physics/physics_config.cc ${RUNTIME_DIR}/physics/physics_commands.cc
    ${RUNTIME_DIR}/physics/physics_layers.cc ${RUNTIME_DIR}/physics/physics_materials.cc
    ${RUNTIME_DIR}/physics/physics_diff.cc ${RUNTIME_DIR}/physics/physics_assets.cc
    ${RUNTIME_DIR}/physics/physics_world.cc ${RUNTIME_DIR}/physics/physics_thread.cc
    ${RUNTIME_DIR}/physics/physics_system.cc ${RUNTIME_DIR}/physics/physics_engine_bridge.cc
    ${RUNTIME_DIR}/physics/physics_bindings.cc ${RUNTIME_DIR}/physics/physics_vm.cc
)
source_group("physics" FILES ${PHYSICS_CC})

# ── Evpp network library (inlined) ─────────────────────────────────
file(GLOB EVPP_SOURCES CONFIGURE_DEPENDS
    ${RUNTIME_DIR}/evpp/*.cc ${RUNTIME_DIR}/evpp/*.h
    ${RUNTIME_DIR}/evpp/evpphttp/*.cc ${RUNTIME_DIR}/evpp/evpphttp/*.h
    ${RUNTIME_DIR}/evpp/http/*.cc ${RUNTIME_DIR}/evpp/http/*.h
    ${RUNTIME_DIR}/evpp/httpc/*.cc ${RUNTIME_DIR}/evpp/httpc/*.h
    ${RUNTIME_DIR}/evpp/udp/*.cc ${RUNTIME_DIR}/evpp/udp/*.h
    ${RUNTIME_DIR}/evpp/kcp/*.cc ${RUNTIME_DIR}/evpp/kcp/*.h
)
source_group("evpp" FILES ${EVPP_SOURCES})

# ── Mongo wrapper (database/mongo/) ───────────────────────────────────
set(MONGO_SOURCES
    ${RUNTIME_DIR}/database/mongo/mongo_apm.cc ${RUNTIME_DIR}/database/mongo/mongo_apm.h
    ${RUNTIME_DIR}/database/mongo/mongo_bson.cc ${RUNTIME_DIR}/database/mongo/mongo_bson.h
    ${RUNTIME_DIR}/database/mongo/mongo_bson_ext.cc ${RUNTIME_DIR}/database/mongo/mongo_bson_ext.h
    ${RUNTIME_DIR}/database/mongo/mongo_bson_vector.cc ${RUNTIME_DIR}/database/mongo/mongo_bson_vector.h
    ${RUNTIME_DIR}/database/mongo/mongo_bulk.cc ${RUNTIME_DIR}/database/mongo/mongo_bulk.h
    ${RUNTIME_DIR}/database/mongo/mongo_bulkwrite.cc ${RUNTIME_DIR}/database/mongo/mongo_bulkwrite.h
    ${RUNTIME_DIR}/database/mongo/mongo_change_stream.cc ${RUNTIME_DIR}/database/mongo/mongo_change_stream.h
    ${RUNTIME_DIR}/database/mongo/mongo_client.cc ${RUNTIME_DIR}/database/mongo/mongo_client.h
    ${RUNTIME_DIR}/database/mongo/mongo_client_pool.cc ${RUNTIME_DIR}/database/mongo/mongo_client_pool.h
    ${RUNTIME_DIR}/database/mongo/mongo_cursor.cc ${RUNTIME_DIR}/database/mongo/mongo_cursor.h
    ${RUNTIME_DIR}/database/mongo/mongo_encryption.cc ${RUNTIME_DIR}/database/mongo/mongo_encryption.h
    ${RUNTIME_DIR}/database/mongo/mongo_error.cc ${RUNTIME_DIR}/database/mongo/mongo_error.h
    ${RUNTIME_DIR}/database/mongo/mongo_find_and_modify_opts.cc ${RUNTIME_DIR}/database/mongo/mongo_find_and_modify_opts.h
    ${RUNTIME_DIR}/database/mongo/mongo_forward.h
    ${RUNTIME_DIR}/database/mongo/mongo_gridfs.cc ${RUNTIME_DIR}/database/mongo/mongo_gridfs.h
    ${RUNTIME_DIR}/database/mongo/mongo_handshake.cc ${RUNTIME_DIR}/database/mongo/mongo_handshake.h
    ${RUNTIME_DIR}/database/mongo/mongo_host_list.cc ${RUNTIME_DIR}/database/mongo/mongo_host_list.h
    ${RUNTIME_DIR}/database/mongo/mongo_index_model.cc ${RUNTIME_DIR}/database/mongo/mongo_index_model.h
    ${RUNTIME_DIR}/database/mongo/mongo_init.cc ${RUNTIME_DIR}/database/mongo/mongo_init.h
    ${RUNTIME_DIR}/database/mongo/mongo_log.cc ${RUNTIME_DIR}/database/mongo/mongo_log.h
    ${RUNTIME_DIR}/database/mongo/mongo_oid.cc ${RUNTIME_DIR}/database/mongo/mongo_oid.h
    ${RUNTIME_DIR}/database/mongo/mongo_oidc.cc ${RUNTIME_DIR}/database/mongo/mongo_oidc.h
    ${RUNTIME_DIR}/database/mongo/mongo_optional.cc ${RUNTIME_DIR}/database/mongo/mongo_optional.h
    ${RUNTIME_DIR}/database/mongo/mongo_rand.cc ${RUNTIME_DIR}/database/mongo/mongo_rand.h
    ${RUNTIME_DIR}/database/mongo/mongo_server_api.cc ${RUNTIME_DIR}/database/mongo/mongo_server_api.h
    ${RUNTIME_DIR}/database/mongo/mongo_session.cc ${RUNTIME_DIR}/database/mongo/mongo_session.h
    ${RUNTIME_DIR}/database/mongo/mongo_settings.cc ${RUNTIME_DIR}/database/mongo/mongo_settings.h
    ${RUNTIME_DIR}/database/mongo/mongo_socket.cc ${RUNTIME_DIR}/database/mongo/mongo_socket.h
    ${RUNTIME_DIR}/database/mongo/mongo_ssl.cc ${RUNTIME_DIR}/database/mongo/mongo_ssl.h
    ${RUNTIME_DIR}/database/mongo/mongo_stream.cc ${RUNTIME_DIR}/database/mongo/mongo_stream.h
    ${RUNTIME_DIR}/database/mongo/mongo_system.cc ${RUNTIME_DIR}/database/mongo/mongo_system.h
    ${RUNTIME_DIR}/database/mongo/mongo_topology.cc ${RUNTIME_DIR}/database/mongo/mongo_topology.h
    ${RUNTIME_DIR}/database/mongo/mongo_uri.cc ${RUNTIME_DIR}/database/mongo/mongo_uri.h
    ${RUNTIME_DIR}/database/mongo/mongo_version.cc ${RUNTIME_DIR}/database/mongo/mongo_version.h
)
source_group("database\\mongo" FILES ${MONGO_SOURCES})

# ── Mongo Lua bindings (database/mongo_bind/) ─────────────────────────
set(MONGO_BIND_SOURCES
    ${RUNTIME_DIR}/database/mongo_bind/mongo_bind.cc ${RUNTIME_DIR}/database/mongo_bind/mongo_bind.h
)
source_group("database\\mongo_bind" FILES ${MONGO_BIND_SOURCES})

# ── Database service (database/data_service/) ──────────────────────────
set(DB_SERVICE_SOURCES
    ${RUNTIME_DIR}/database/data_service/database_service.cc ${RUNTIME_DIR}/database/data_service/database_service.h
    ${RUNTIME_DIR}/database/data_service/db_thread.cc ${RUNTIME_DIR}/database/data_service/db_thread.h
    ${RUNTIME_DIR}/database/data_service/db_script_vm.cc ${RUNTIME_DIR}/database/data_service/db_script_vm.h
    ${RUNTIME_DIR}/database/data_service/db_request.h
    ${RUNTIME_DIR}/database/data_service/db_service_config.h
    ${RUNTIME_DIR}/database/data_service/module_access.h
)
source_group("database\\data_service" FILES ${DB_SERVICE_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# Combined sources
# ═══════════════════════════════════════════════════════════════════════

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
)

if(ENGINE_MONGODB_ENABLED)
    list(APPEND SERVERENGINE_SOURCES ${MONGO_SOURCES} ${MONGO_BIND_SOURCES} ${DB_SERVICE_SOURCES})
endif()

if(ENGINE_PHYSICS_ENABLED)
    list(APPEND SERVERENGINE_SOURCES ${PHYSICS_CC})
endif()
