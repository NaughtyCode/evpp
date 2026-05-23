cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENGINE_BUILD triggers dllexport in engine_export.h.
# Consumers who link against this library do NOT define this
# and will get dllimport on Windows / no-op elsewhere.
add_definitions(-DENGINE_BUILD)

include_directories(${PROJECT_SOURCE_DIR}/3rdparty/quill/include)
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/lua)
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/glaze/include)
include_directories(${PROJECT_SOURCE_DIR})  # for "engine/xxx.h" includes

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

if (UNIX)
    set(LIBRARIES evpp_concurrentqueue event glog pthread)
else (UNIX)
    set(LIBRARIES evpp_static event glog)
endif (UNIX)

# ── Lua library (amalgamated build) ──────────────────────────────────
set(LUA_SOURCES
    ${PROJECT_SOURCE_DIR}/3rdparty/lua/onelua.c
)
set_source_files_properties(${LUA_SOURCES} PROPERTIES COMPILE_DEFINITIONS "MAKE_LIB")
source_group("3rdparty\\lua" FILES ${LUA_SOURCES})

# ── Log module ────────────────────────────────────────────────────
set(LOG_SOURCES
    core/log/log.cc
    core/log/log.h
    core/log/log_levels.h
    core/log/log_context.h
    core/log/log_macros.h
)
source_group("engine\\core\\log" FILES ${LOG_SOURCES})

# ── Timer module ──────────────────────────────────────────────────
set(TIMER_SOURCES
    core/timer/timer_manager.cc
    core/timer/timer_manager.h
    core/timer/timer.h
    core/timer/timer_core.h
    core/timer/timer_queue.h
    core/timer/clock_source.h
    core/timer/hr_timer.h
    core/timer/timer_wheel.h
    core/timer/alarm_timer.h
)
source_group("engine\\core\\timer" FILES ${TIMER_SOURCES})

# ── Config module ──────────────────────────────────────────────────
set(CONFIG_SOURCES
    config/config.cc
    config/config.h
)
source_group("engine\\config" FILES ${CONFIG_SOURCES})

# ── Engine module ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    engine/engine.cc
    engine/engine.h
)
source_group("engine\\engine" FILES ${ENGINE_SOURCES})

# ── VM module ────────────────────────────────────────────────────
set(VM_SOURCES
    vm/vm.cc
    vm/vm.h
    vm/script_importer.cc
    vm/script_importer.h
)
source_group("engine\\vm" FILES ${VM_SOURCES})

# ── Script bindings ──────────────────────────────────────────────
set(SCRIPT_SOURCES
    script/script_bind.cc
    script/script_bind.h
    script/log_bind.cc
    script/log_bind.h
    script/timer_bind.cc
    script/timer_bind.h
    script/net_bind.cc
    script/net_bind.h
    script/msgpack_bind.cc
    script/msgpack_bind.h
    script/import_bind.cc
    script/import_bind.h
)
source_group("engine\\script" FILES ${SCRIPT_SOURCES})

# ── Export header ─────────────────────────────────────────────────
set(EXPORT_HEADER
    engine_export.h
)
source_group("engine" FILES ${EXPORT_HEADER})

add_library(engine SHARED
    ${LUA_SOURCES}
    ${LOG_SOURCES}
    ${TIMER_SOURCES}
    ${CONFIG_SOURCES}
    ${ENGINE_SOURCES}
    ${VM_SOURCES}
    ${SCRIPT_SOURCES}
    ${EXPORT_HEADER}
)

target_include_directories(engine PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/3rdparty/quill/include
    ${PROJECT_SOURCE_DIR}/3rdparty/lua
)

# ── Physics module ──────────────────────────────────────────────────
add_subdirectory(physics)

target_link_libraries(engine ${LIBRARIES} engine_physics)

# Copy engine DLL to bin directory so main.exe can find it at runtime
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:engine>"
    "${PROJECT_SOURCE_DIR}/artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:engine>"
    COMMENT "Copying engine.dll to bin/"
)
