cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENGINE_BUILD triggers dllexport in engine_export.h.
# Consumers who link against this library do NOT define this
# and will get dllimport on Windows / no-op elsewhere.
add_definitions(-DENGINE_BUILD)

option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" OFF)

include_directories(${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/lua/lua)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/glaze/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})  # for "engine/xxx.h", "core/xxx.h", "evpp/xxx.h" etc.
include_directories(${PROJECT_SOURCE_DIR})  # for "thirdparty/xxx.h" includes

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_compile_options(/bigobj /Zc:preprocessor /utf-8)
endif()

if (UNIX)
    set(LIBRARIES event glog pthread)
else (UNIX)
    set(LIBRARIES event glog)
endif (UNIX)

# ── Lua library (amalgamated build) ──────────────────────────────────
set(LUA_SOURCES
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua/onelua.c
)
set_source_files_properties(${LUA_SOURCES} PROPERTIES COMPILE_DEFINITIONS "MAKE_LIB")
source_group("thirdparty\\lua" FILES ${LUA_SOURCES})

# ── Perfetto amalgamated source ──────────────────────────────────────
if(ENGINE_PROFILER_ENABLED)
    set(PERFETTO_SOURCES
        ${PROJECT_SOURCE_DIR}/thirdparty/perfetto/perfetto.cc
    )
    source_group("thirdparty\\perfetto" FILES ${PERFETTO_SOURCES})
else()
    set(PERFETTO_SOURCES "")
endif()

# ── Log module ────────────────────────────────────────────────────
set(LOG_SOURCES
    core/log/log.cc
    core/log/log.h
    core/log/log_levels.h
    core/log/log_context.h
    core/log/log_macros.h
)
source_group("core\\log" FILES ${LOG_SOURCES})

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
source_group("core\\timer" FILES ${TIMER_SOURCES})

# ── Config module ──────────────────────────────────────────────────
set(CONFIG_SOURCES
    config/config.cc
    config/config.h
)
source_group("config" FILES ${CONFIG_SOURCES})

# ── Engine module ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    engine/engine.cc
    engine/engine.h
)
source_group("engine" FILES ${ENGINE_SOURCES})

# ── VM module ────────────────────────────────────────────────────
set(VM_SOURCES
    vm/vm.cc
    vm/vm.h
    vm/script_importer.cc
    vm/script_importer.h
)
source_group("vm" FILES ${VM_SOURCES})

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
source_group("script" FILES ${SCRIPT_SOURCES})

# ── Profiler module ─────────────────────────────────────────────────
set(PROFILER_SOURCES
    profiler/profiler_categories.h
    profiler/profiler_core.cc
    profiler/profiler_core.h
    profiler/profiler_events.h
    profiler/profiler_macros.h
)
source_group("profiler" FILES ${PROFILER_SOURCES})

# ── Export header ─────────────────────────────────────────────────
set(EXPORT_HEADER
    engine_export.h
)
source_group("runtime" FILES ${EXPORT_HEADER})

# ── Evpp network library (inlined into engine) ──────────────────────────
file(GLOB EVPP_SOURCES
    evpp/*.cc evpp/*.h
    evpp/evpphttp/*.cc evpp/evpphttp/*.h
    evpp/http/*.cc evpp/http/*.h
    evpp/httpc/*.cc evpp/httpc/*.h
    evpp/udp/*.cc evpp/udp/*.h
)
source_group("evpp" FILES ${EVPP_SOURCES})

add_library(engine SHARED
    ${LUA_SOURCES}
    ${PERFETTO_SOURCES}
    ${LOG_SOURCES}
    ${TIMER_SOURCES}
    ${CONFIG_SOURCES}
    ${ENGINE_SOURCES}
    ${VM_SOURCES}
    ${SCRIPT_SOURCES}
    ${PROFILER_SOURCES}
    ${EXPORT_HEADER}
    ${EVPP_SOURCES}
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/thirdparty/quill/quill/include
    ${PROJECT_SOURCE_DIR}/thirdparty/lua/lua
)

if(ENGINE_PROFILER_ENABLED)
    target_compile_definitions(engine PUBLIC ENGINE_PROFILER_ENABLED)
endif()

# ── JoltPhysics library ──────────────────────────────────────────────
# JoltPhysics v5.5.1 (https://github.com/jrouwe/JoltPhysics, MIT License)
# Must be available before engine_physics links against it.
# Note: Jolt.cmake sets CMAKE_CXX_STANDARD to 17 globally — we restore
# C++23 for downstream targets.
add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/JoltPhysics/JoltPhysics/Build
                 ${PROJECT_SOURCE_DIR}/../artifacts/build/jolt)
set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Physics module ──────────────────────────────────────────────────
add_subdirectory(physics)

target_link_libraries(engine ${LIBRARIES} engine_physics)

# Copy engine DLL to bin directory so main.exe can find it at runtime
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:engine>"
    "${PROJECT_SOURCE_DIR}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:engine>"
    COMMENT "Copying engine.dll to bin/"
)
