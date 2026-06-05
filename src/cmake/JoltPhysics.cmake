function(configure_engine_jolt_physics engine_root)
    set(_jolt_vendor_dir "${engine_root}/thirdparty/JoltPhysics")
    set(_jolt_repo_dir "")
    foreach(_candidate IN ITEMS
            "${_jolt_vendor_dir}"
            "${_jolt_vendor_dir}/JoltPhysics")
        if(EXISTS "${_candidate}/Jolt/Jolt.cmake")
            set(_jolt_repo_dir "${_candidate}")
            break()
        endif()
    endforeach()

    if(_jolt_repo_dir)
        set(ENGINE_JOLT_PHYSICS_ROOT "${_jolt_repo_dir}"
            CACHE INTERNAL "Resolved JoltPhysics include root" FORCE)
    else()
        set(_jolt_build_dir "${_jolt_vendor_dir}/JoltPhysics/Build")
        if(EXISTS "${_jolt_build_dir}/CMakeLists.txt")
            set(ENGINE_JOLT_PHYSICS_ROOT "${_jolt_build_dir}/.."
                CACHE INTERNAL "Resolved JoltPhysics include root" FORCE)
        else()
            message(FATAL_ERROR
                "ENGINE_PHYSICS_ENABLED=ON requires vendored JoltPhysics sources at "
                "${_jolt_vendor_dir}/Jolt/Jolt.cmake or "
                "${_jolt_vendor_dir}/JoltPhysics/Build/CMakeLists.txt.")
        endif()
    endif()

    if(TARGET Jolt)
        return()
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
    set(ENABLE_OBJECT_STREAM ON CACHE BOOL "Enable Jolt object stream support" FORCE)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED ON CACHE BOOL "Enable Jolt FP exceptions in MSVC Debug/Release" FORCE)

    if(MSVC AND (CMAKE_VS_PLATFORM_NAME STREQUAL "x64" OR
                 CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64"))
        set(USE_AVX2 ON CACHE BOOL "Enable Jolt AVX2 code paths" FORCE)
        set(USE_AVX ON CACHE BOOL "Enable Jolt AVX code paths" FORCE)
        set(USE_SSE4_1 ON CACHE BOOL "Enable Jolt SSE4.1 code paths" FORCE)
        set(USE_SSE4_2 ON CACHE BOOL "Enable Jolt SSE4.2 code paths" FORCE)
        set(USE_LZCNT ON CACHE BOOL "Enable Jolt LZCNT code paths" FORCE)
        set(USE_TZCNT ON CACHE BOOL "Enable Jolt TZCNT code paths" FORCE)
        set(USE_F16C ON CACHE BOOL "Enable Jolt F16C code paths" FORCE)
        set(USE_FMADD ON CACHE BOOL "Enable Jolt FMA code paths" FORCE)
    endif()

    if(_jolt_repo_dir)
        set(PHYSICS_REPO_ROOT "${_jolt_repo_dir}")
        include("${_jolt_repo_dir}/Jolt/Jolt.cmake")
    else()
        add_subdirectory("${_jolt_build_dir}" "${CMAKE_BINARY_DIR}/jolt" EXCLUDE_FROM_ALL)
    endif()

    set_target_properties(Jolt PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
endfunction()
