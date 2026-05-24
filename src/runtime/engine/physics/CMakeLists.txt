cmake_minimum_required(VERSION 3.8)

#============================================================================
# engine/physics/CMakeLists.txt — Physics system build configuration
#
# ENGINE_PHYSICS_ENABLED macro controls conditional compilation:
#   - Defined (server build): all physics sources compile and link.
#   - Not defined (client build): engine_physics is an INTERFACE library
#     with no sources; all #ifdef-guarded code compiles to empty stubs.
#============================================================================

set(PHYSICS_SOURCES
    physics_config.cc
    physics_config.h
    physics_commands.cc
    physics_commands.h
    physics_layers.cc
    physics_layers.h
    physics_materials.cc
    physics_materials.h
    physics_diff.cc
    physics_diff.h
    physics_assets.cc
    physics_assets.h
    physics_world.cc
    physics_world.h
    physics_thread.cc
    physics_thread.h
    physics_system.cc
    physics_system.h
    physics_engine_bridge.cc
    physics_engine_bridge.h
    physics_bindings.cc
    physics_bindings.h
)

source_group("engine\\physics" FILES ${PHYSICS_SOURCES})

if(ENGINE_PHYSICS_ENABLED)
    add_library(engine_physics STATIC ${PHYSICS_SOURCES})

    target_compile_definitions(engine_physics PUBLIC ENGINE_PHYSICS_ENABLED)

    if(ENGINE_PROFILER_ENABLED)
        target_compile_definitions(engine_physics PRIVATE ENGINE_PROFILER_ENABLED)
    endif()

    if(MSVC)
        target_compile_options(engine_physics PRIVATE /FS /Z7)
    endif()

    target_include_directories(engine_physics PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/3rdparty/JoltPhysics/JoltPhysics
        ${PROJECT_SOURCE_DIR}/3rdparty/quill/quill/include
        ${PROJECT_SOURCE_DIR}/3rdparty/glaze/include
        ${PROJECT_SOURCE_DIR}/3rdparty/concurrentqueue/concurrentqueue
        ${PROJECT_SOURCE_DIR}/3rdparty/lua/lua
    )

    target_link_libraries(engine_physics
        Jolt
    )
else()
    # Physics disabled: INTERFACE library with no sources.
    # Engine code referencing physics includes is protected by #ifdef guards,
    # and the bridge header has inline empty stubs.
    add_library(engine_physics INTERFACE)
endif()
