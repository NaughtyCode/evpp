set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/3rdparty/quill/include)

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

if (UNIX)
    set(LIBRARIES evpp_concurrentqueue event glog pthread)
else (UNIX)
    set(LIBRARIES evpp_static event glog)
endif (UNIX)

# ── Log module ────────────────────────────────────────────────────
set(LOG_SOURCES
    ../core/log/log.cc
    ../core/log/log.h
    ../core/log/log_levels.h
    ../core/log/log_config.h
    ../core/log/log_context.h
    ../core/log/log_macros.h
)
source_group("engine\\core\\log" FILES ${LOG_SOURCES})

# ── Timer module ──────────────────────────────────────────────────
set(TIMER_SOURCES
    ../core/timer/timer_manager.cc
    ../core/timer/timer_manager.h
    ../core/timer/timer.h
    ../core/timer/timer_core.h
    ../core/timer/timer_queue.h
    ../core/timer/clock_source.h
    ../core/timer/hr_timer.h
    ../core/timer/timer_wheel.h
    ../core/timer/alarm_timer.h
)
source_group("engine\\core\\timer" FILES ${TIMER_SOURCES})

# ── Engine module ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    ../engine/engine.cc
    ../engine/engine.h
)
source_group("engine\\engine" FILES ${ENGINE_SOURCES})

# ── Main entry ────────────────────────────────────────────────────
source_group("" FILES main.cc)

add_executable(engine_main
    main.cc
    ${LOG_SOURCES}
    ${TIMER_SOURCES}
    ${ENGINE_SOURCES}
)

target_link_libraries(engine_main ${LIBRARIES})
