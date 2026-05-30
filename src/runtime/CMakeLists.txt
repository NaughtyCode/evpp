# Pure file-organization module — no compilation options, no targets.
# Include via include() to obtain source-file lists and source_group
# definitions for everything under src/runtime/.
#
# Provides:
#   SERVERENGINE_SOURCES  — combined source list for runtime targets
#   Individual lists: LUA_SOURCES, LOG_SOURCES, TIMER_SOURCES, etc.

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# Absolute path to this (runtime/) source directory.
set(RUNTIME_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ═══════════════════════════════════════════════════════════════════════
# Third-party sources
# ═══════════════════════════════════════════════════════════════════════

# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
include(${ENGINE_ROOT}/thirdparty/lua/lua_sources.cmake)
set(LUA_CORE_SOURCES ${ENGINE_LUA_CORE_SOURCES})
set(LUA_LIB_SOURCES ${ENGINE_LUA_LIB_SOURCES})
set(LUA_SOURCES ${ENGINE_LUA_SOURCES})
source_group("thirdparty\\lua" FILES ${LUA_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# Engine modules
# ═══════════════════════════════════════════════════════════════════════

set(EXPORT_HEADER ${RUNTIME_DIR}/core/engine_api.h)
source_group("core" FILES ${EXPORT_HEADER})

# ── Memory allocator wrapper ──────────────────────────────────────────
set(MEM_SOURCES
    ${RUNTIME_DIR}/core/mem/mem.h
    ${RUNTIME_DIR}/core/mem/mem_stats.h
    ${RUNTIME_DIR}/core/mem/mem_stats.cc
)
source_group("core\\mem" FILES ${MEM_SOURCES})

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
set(CONFIG_SOURCES
    ${RUNTIME_DIR}/config/config.cc ${RUNTIME_DIR}/config/config.h
    ${RUNTIME_DIR}/config/config_constants.h ${RUNTIME_DIR}/config/limits.h
    ${RUNTIME_DIR}/config/config_validator.cc ${RUNTIME_DIR}/config/config_validator.h
    ${RUNTIME_DIR}/config/config_bind.cc ${RUNTIME_DIR}/config/config_bind.h
    ${RUNTIME_DIR}/config/config_table.cc ${RUNTIME_DIR}/config/config_table.h
    ${RUNTIME_DIR}/config/reference_validator.cc ${RUNTIME_DIR}/config/reference_validator.h)
source_group("config" FILES ${CONFIG_SOURCES})

# ── Network codec ───────────────────────────────────────────────────
set(NETWORK_SOURCES
	${RUNTIME_DIR}/network/length_prefixed_codec.cc ${RUNTIME_DIR}/network/length_prefixed_codec.h
)
source_group("network" FILES ${NETWORK_SOURCES})

# ── Engine core ────────────────────────────────────────────────────
set(ENGINE_SOURCES ${RUNTIME_DIR}/engine/engine.cc ${RUNTIME_DIR}/engine/engine.h)
source_group("engine" FILES ${ENGINE_SOURCES})

# ── Script VM ─────────────────────────────────────────────────────
set(VM_SOURCES
    ${RUNTIME_DIR}/vm/vm.cc ${RUNTIME_DIR}/vm/vm.h
    ${RUNTIME_DIR}/vm/sandbox.cc ${RUNTIME_DIR}/vm/sandbox.h
    ${RUNTIME_DIR}/vm/script_importer.cc ${RUNTIME_DIR}/vm/script_importer.h
    ${RUNTIME_DIR}/vm/custom_ptr_store.cc ${RUNTIME_DIR}/vm/custom_ptr_store.h
    ${RUNTIME_DIR}/vm/coroutine_scheduler.cc ${RUNTIME_DIR}/vm/coroutine_scheduler.h
    ${RUNTIME_DIR}/vm/script_reloader.cc ${RUNTIME_DIR}/vm/script_reloader.h
    ${RUNTIME_DIR}/vm/file_watcher.cc ${RUNTIME_DIR}/vm/file_watcher.h
)
source_group("vm" FILES ${VM_SOURCES})

# ── Entity system ──────────────────────────────────────────────────
set(ENTITY_SOURCES
    ${RUNTIME_DIR}/entity/entity_id.h
    ${RUNTIME_DIR}/entity/attribute.h
    ${RUNTIME_DIR}/entity/entity.cc ${RUNTIME_DIR}/entity/entity.h
    ${RUNTIME_DIR}/entity/entity_manager.cc ${RUNTIME_DIR}/entity/entity_manager.h
)
source_group("entity" FILES ${ENTITY_SOURCES})

# ── Space / connection routing ──────────────────────────────────────
set(SPACE_SOURCES
    ${RUNTIME_DIR}/space/connection_router.cc ${RUNTIME_DIR}/space/connection_router.h
    ${RUNTIME_DIR}/space/space_manager.cc ${RUNTIME_DIR}/space/space_manager.h
    ${RUNTIME_DIR}/space/space.cc ${RUNTIME_DIR}/space/space.h
    ${RUNTIME_DIR}/space/space_message.cc ${RUNTIME_DIR}/space/space_message.h
)
source_group("space" FILES ${SPACE_SOURCES})

# ── RPC ──────────────────────────────────────────────────────────────
set(RPC_SOURCES
    ${RUNTIME_DIR}/rpc/rpc_client.cc ${RUNTIME_DIR}/rpc/rpc_client.h
    ${RUNTIME_DIR}/rpc/rpc_server.cc ${RUNTIME_DIR}/rpc/rpc_server.h
    ${RUNTIME_DIR}/rpc/rpc_protocol.h
)
source_group("rpc" FILES ${RPC_SOURCES})

# ── Auth ─────────────────────────────────────────────────────────────
set(AUTH_SOURCES
    ${RUNTIME_DIR}/auth/auth_backend.cc ${RUNTIME_DIR}/auth/auth_backend.h
    ${RUNTIME_DIR}/auth/session_manager.cc ${RUNTIME_DIR}/auth/session_manager.h
    ${RUNTIME_DIR}/auth/auth_protocol.h
)
source_group("auth" FILES ${AUTH_SOURCES})

# ── Script bindings ────────────────────────────────────────────────
set(SCRIPT_SOURCES
    ${RUNTIME_DIR}/script/bind_util.cc ${RUNTIME_DIR}/script/bind_util.h
    ${RUNTIME_DIR}/script/net_lifetime.cc ${RUNTIME_DIR}/script/net_lifetime.h
    ${RUNTIME_DIR}/script/script_bind.cc ${RUNTIME_DIR}/script/script_bind.h
    ${RUNTIME_DIR}/script/log_bind.cc ${RUNTIME_DIR}/script/log_bind.h
    ${RUNTIME_DIR}/script/timer_bind.cc ${RUNTIME_DIR}/script/timer_bind.h
    ${RUNTIME_DIR}/script/net_bind.cc ${RUNTIME_DIR}/script/net_bind.h
    ${RUNTIME_DIR}/script/net_tcp_client_bind.cc ${RUNTIME_DIR}/script/net_tcp_client_bind.h
    ${RUNTIME_DIR}/script/net_tcp_server_bind.cc ${RUNTIME_DIR}/script/net_tcp_server_bind.h
    ${RUNTIME_DIR}/script/net_http_bind.cc ${RUNTIME_DIR}/script/net_http_bind.h
    ${RUNTIME_DIR}/script/net_udp_client_bind.cc ${RUNTIME_DIR}/script/net_udp_client_bind.h
    ${RUNTIME_DIR}/script/net_udp_server_bind.cc ${RUNTIME_DIR}/script/net_udp_server_bind.h
    ${RUNTIME_DIR}/script/net_kcp_client_bind.cc ${RUNTIME_DIR}/script/net_kcp_client_bind.h
    ${RUNTIME_DIR}/script/net_kcp_server_bind.cc ${RUNTIME_DIR}/script/net_kcp_server_bind.h
    ${RUNTIME_DIR}/script/msgpack_bind.cc ${RUNTIME_DIR}/script/msgpack_bind.h
    ${RUNTIME_DIR}/script/import_bind.cc ${RUNTIME_DIR}/script/import_bind.h
    ${RUNTIME_DIR}/script/entity_bind.cc ${RUNTIME_DIR}/script/entity_bind.h
    ${RUNTIME_DIR}/script/space_bind.cc ${RUNTIME_DIR}/script/space_bind.h
    ${RUNTIME_DIR}/script/aoi_bind.cc ${RUNTIME_DIR}/script/aoi_bind.h
    ${RUNTIME_DIR}/script/rpc_bind.cc ${RUNTIME_DIR}/script/rpc_bind.h
    ${RUNTIME_DIR}/script/auth_bind.cc ${RUNTIME_DIR}/script/auth_bind.h
    ${RUNTIME_DIR}/script/mem_bind.cc ${RUNTIME_DIR}/script/mem_bind.h
)
source_group("script" FILES ${SCRIPT_SOURCES})

# ── Monitoring ──────────────────────────────────────────────────────
set(MONITORING_SOURCES
    ${RUNTIME_DIR}/monitoring/metrics.cc ${RUNTIME_DIR}/monitoring/metrics.h
    ${RUNTIME_DIR}/monitoring/admin_http.cc ${RUNTIME_DIR}/monitoring/admin_http.h
)
source_group("monitoring" FILES ${MONITORING_SOURCES})

# ── AOI ──────────────────────────────────────────────────────────────
set(AOI_SOURCES
    ${RUNTIME_DIR}/aoi/spatial_index.cc ${RUNTIME_DIR}/aoi/spatial_index.h
    ${RUNTIME_DIR}/aoi/aoi_manager.cc ${RUNTIME_DIR}/aoi/aoi_manager.h
)
source_group("aoi" FILES ${AOI_SOURCES})

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

# ── ORM + Cache ────────────────────────────────────────────────────
set(ORM_SOURCES
    ${RUNTIME_DIR}/database/orm.cc ${RUNTIME_DIR}/database/orm.h
    ${RUNTIME_DIR}/database/cache.h
)
source_group("database" FILES ${ORM_SOURCES})
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
    ${RUNTIME_DIR}/database/mongo_bind/bind_util.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bson_document.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bson_document.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bson_iter.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bson_iter.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_oid.cc ${RUNTIME_DIR}/database/mongo_bind/bind_oid.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_array_builder.cc ${RUNTIME_DIR}/database/mongo_bind/bind_array_builder.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_uri.cc ${RUNTIME_DIR}/database/mongo_bind/bind_uri.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_client.cc ${RUNTIME_DIR}/database/mongo_bind/bind_client.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_database.cc ${RUNTIME_DIR}/database/mongo_bind/bind_database.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_collection.cc ${RUNTIME_DIR}/database/mongo_bind/bind_collection.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_cursor.cc ${RUNTIME_DIR}/database/mongo_bind/bind_cursor.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_client_pool.cc ${RUNTIME_DIR}/database/mongo_bind/bind_client_pool.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_session.cc ${RUNTIME_DIR}/database/mongo_bind/bind_session.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_transaction_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_transaction_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_session_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_session_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_change_stream.cc ${RUNTIME_DIR}/database/mongo_bind/bind_change_stream.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_read_prefs.cc ${RUNTIME_DIR}/database/mongo_bind/bind_read_prefs.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_write_concern.cc ${RUNTIME_DIR}/database/mongo_bind/bind_write_concern.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_read_concern.cc ${RUNTIME_DIR}/database/mongo_bind/bind_read_concern.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_operation.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_operation.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulkwrite.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulkwrite.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_insert_one_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_insert_one_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_update_one_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_update_one_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_update_many_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_update_many_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_replace_one_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_replace_one_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_delete_one_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_delete_one_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_delete_many_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_delete_many_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_result.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_result.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_exception.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bulk_write_exception.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_server_api.cc ${RUNTIME_DIR}/database/mongo_bind/bind_server_api.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_find_and_modify_opts.cc ${RUNTIME_DIR}/database/mongo_bind/bind_find_and_modify_opts.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_error.cc ${RUNTIME_DIR}/database/mongo_bind/bind_error.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_host_list.cc ${RUNTIME_DIR}/database/mongo_bind/bind_host_list.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_ssl.cc ${RUNTIME_DIR}/database/mongo_bind/bind_ssl.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_gridfs.cc ${RUNTIME_DIR}/database/mongo_bind/bind_gridfs.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_index_model.cc ${RUNTIME_DIR}/database/mongo_bind/bind_index_model.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_topology.cc ${RUNTIME_DIR}/database/mongo_bind/bind_topology.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bson_vector.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bson_vector.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_bson_ext.cc ${RUNTIME_DIR}/database/mongo_bind/bind_bson_ext.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_stream.cc ${RUNTIME_DIR}/database/mongo_bind/bind_stream.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_socket.cc ${RUNTIME_DIR}/database/mongo_bind/bind_socket.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_log.cc ${RUNTIME_DIR}/database/mongo_bind/bind_log.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_oidc.cc ${RUNTIME_DIR}/database/mongo_bind/bind_oidc.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_encryption.cc ${RUNTIME_DIR}/database/mongo_bind/bind_encryption.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_apm.cc ${RUNTIME_DIR}/database/mongo_bind/bind_apm.h
    ${RUNTIME_DIR}/database/mongo_bind/bind_misc.cc ${RUNTIME_DIR}/database/mongo_bind/bind_misc.h
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
    ${RUNTIME_DIR}/database/data_service/db_service_main_bind.cc ${RUNTIME_DIR}/database/data_service/db_service_main_bind.h
)
source_group("database\\data_service" FILES ${DB_SERVICE_SOURCES})

# ═══════════════════════════════════════════════════════════════════════
# Combined sources
# ═══════════════════════════════════════════════════════════════════════

set(SERVERENGINE_SOURCES
    ${EXPORT_HEADER}
    ${MEM_SOURCES}
    ${LOG_SOURCES}
    ${TIMER_SOURCES}
    ${CONFIG_SOURCES}
    ${NETWORK_SOURCES}
    ${ENGINE_SOURCES}
    ${VM_SOURCES}
    ${ENTITY_SOURCES}
    ${SPACE_SOURCES}
    ${RPC_SOURCES}
    ${AUTH_SOURCES}
    ${SCRIPT_SOURCES}
    ${MONITORING_SOURCES}
    ${AOI_SOURCES}
    ${PROFILER_SOURCES}
    ${PHYSICS_HEADERS}
    ${EVPP_SOURCES}
)

if(ENGINE_MONGODB_ENABLED)
    list(APPEND SERVERENGINE_SOURCES ${MONGO_SOURCES} ${MONGO_BIND_SOURCES} ${DB_SERVICE_SOURCES} ${ORM_SOURCES}
        ${RUNTIME_DIR}/script/orm_bind.cc ${RUNTIME_DIR}/script/orm_bind.h)
endif()

if(ENGINE_PHYSICS_ENABLED)
    list(APPEND SERVERENGINE_SOURCES ${PHYSICS_CC})
endif()
