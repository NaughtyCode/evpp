function(configure_engine_jolt_physics engine_root)
    if(TARGET Jolt)
        return()
    endif()

    set(_jolt_build_dir "${engine_root}/thirdparty/JoltPhysics/JoltPhysics/Build")
    if(NOT EXISTS "${_jolt_build_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "ENGINE_PHYSICS_ENABLED=ON requires vendored JoltPhysics Build/CMakeLists.txt at "
            "${_jolt_build_dir}. The upstream Jolt Build directory must be present and tracked.")
    endif()

    # The engine only needs Jolt's core physics library. Keep optional renderer,
    # compute shader, install, and strict warning surfaces out of the server build.
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Disable Jolt third-party warnings-as-errors" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "Disable Jolt install targets" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use the engine MSVC runtime setting" FORCE)
    set(JPH_USE_DX12 OFF CACHE BOOL "Disable Jolt DirectX compute backend" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "Disable Jolt Vulkan compute backend" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "Disable Jolt Metal compute backend" FORCE)
    set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "Disable Jolt CPU compute backend" FORCE)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "Disable Jolt debug renderer" FORCE)
    set(DEBUG_RENDERER_IN_DISTRIBUTION OFF CACHE BOOL "Disable Jolt distribution debug renderer" FORCE)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "Disable Jolt internal profiler" FORCE)
    set(PROFILER_IN_DISTRIBUTION OFF CACHE BOOL "Disable Jolt distribution profiler" FORCE)

    add_subdirectory("${_jolt_build_dir}" "${CMAKE_BINARY_DIR}/jolt" EXCLUDE_FROM_ALL)
    set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
endfunction()
