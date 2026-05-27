cmake_minimum_required(VERSION 3.16)
project(CullingEngine VERSION 1.6.0 LANGUAGES CXX)

# ============================================================
# Platform detection
# ============================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(CullingEngine_ANDROID TRUE)
    set(CullingEngine_PLATFORM "Android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CullingEngine_IOS TRUE)
    set(CullingEngine_PLATFORM "iOS")
elseif(APPLE)
    set(CullingEngine_OSX TRUE)
    set(CullingEngine_PLATFORM "macOS")
elseif(WIN32)
    set(CullingEngine_WIN TRUE)
    set(CullingEngine_PLATFORM "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CullingEngine_LINUX TRUE)
    set(CullingEngine_PLATFORM "Linux")
endif()

# ============================================================
# Project options
# ============================================================
option(CullingEngine_BUILD_SHARED "Build CullingEngine as shared library" OFF)
option(CullingEngine_BUILD_TESTS "Build CullingEngine test programs" ON)
option(CullingEngine_BUILD_RENDER "Build CullingEngine render programs" ON)
option(CullingEngine_NATIVE_DEBUG "Enable native debug features" ON)

# ============================================================
# Compile definitions
# ============================================================
set(CullingEngine_COMPILE_DEFINITIONS "")

# Static library define
if(NOT CullingEngine_BUILD_SHARED)
    list(APPEND CullingEngine_COMPILE_DEFINITIONS CULLING_ENGINE_STATIC)
    list(APPEND CullingEngine_COMPILE_DEFINITIONS CULLING_ENGINE_USE_INTERNAL)
endif()

# Platform macros are normalized in CullingEngineMacros.h.
if(CullingEngine_WIN)
    # Windows-specific: disable min/max macros
    add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
endif()

# Debug features
if(CullingEngine_NATIVE_DEBUG)
    list(APPEND CullingEngine_COMPILE_DEFINITIONS CULLING_ENGINE_NATIVE_DEBUG)
endif()

# ============================================================
# Library sources
# ============================================================
set(CullingEngine_HEADERS
    include/CullingEngineMacros.h
    include/CullingEngineAPI.h
    include/CullingEngineCore.h
    include/CullingEngineImageDump.h
    include/CullingEngineMeshBaker.h
    include/CullingEngineDebugConfiguration.h
    include/CullingEngineFrameInfo.h
    include/CullingEngineLog.h
    include/CullingEngineLogger.h
    include/MathUtility.h
    include/Vector3D.h
    include/PlatformSIMD.h
    include/MemoryUtility.h
    include/SimdArmNeonTranslation.h
    include/SimdX86Intrinsics.h
    include/OccluderManager.h
    include/OccluderQuadDecomposition.h
    include/SoftwareRasterizer.h
)

set(CullingEngine_SOURCES
    src/CullingEngineAPI.cpp
    src/CullingEngineCaptureReplay.cpp
    src/CullingEngineConfiguration.cpp
    src/CullingEngineCore.cpp
    src/CullingEngineImageDump.cpp
    src/CullingEngineImageWriter.cpp
    src/CullingEngineMeshBaker.cpp
    src/CullingEngineFrameCapture.cpp
    src/CullingEngineFrameInfo.cpp
    src/CullingEngineLog.cpp
    src/CullingEngineLogger.cpp
    src/CullingEngineSync.cpp
    src/Matrix4x4Debug.cpp
    src/OccluderManager.cpp
    src/OccluderQuadDecomposition.cpp
    src/QueryDebugStates.cpp
    src/SoftwareRasterizer.cpp
    src/SoftwareRasterizerDebug.cpp
)

set(CullingEngine_RASTERIZER_FRAGMENTS
    src/SoftwareRasterizer/SoftwareRasterizerCommon.inl
    src/SoftwareRasterizer/SoftwareRasterizerLifecycle.inl
    src/SoftwareRasterizer/SoftwareRasterizerQuery.inl
    src/SoftwareRasterizer/SoftwareRasterizerReadback.inl
    src/SoftwareRasterizer/SoftwareRasterizerTables.inl
    src/SoftwareRasterizer/SoftwareRasterizerRasterize.inl
    src/SoftwareRasterizer/SoftwareRasterizerBlocks.inl
    src/SoftwareRasterizer/SoftwareRasterizerTriangles.inl
    src/SoftwareRasterizer/SoftwareRasterizerDebugDraw.inl
    src/SoftwareRasterizer/SoftwareRasterizerInstantiations.inl
)
set_source_files_properties(${CullingEngine_RASTERIZER_FRAGMENTS} PROPERTIES HEADER_FILE_ONLY TRUE)

# ============================================================
# Library target
# ============================================================
if(CullingEngine_BUILD_SHARED)
    add_library(CullingEngine SHARED ${CullingEngine_SOURCES} ${CullingEngine_HEADERS} ${CullingEngine_RASTERIZER_FRAGMENTS})
    set(CullingEngine_PRIVATE_COMPILE_DEFINITIONS CULLING_ENGINE_LIB_EXPORT)
else()
    add_library(CullingEngine STATIC ${CullingEngine_SOURCES} ${CullingEngine_HEADERS} ${CullingEngine_RASTERIZER_FRAGMENTS})
    set(CullingEngine_PRIVATE_COMPILE_DEFINITIONS "")
endif()

target_include_directories(CullingEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(CullingEngine
    PUBLIC ${CullingEngine_COMPILE_DEFINITIONS}
    PRIVATE ${CullingEngine_PRIVATE_COMPILE_DEFINITIONS}
)

# ============================================================
# Compiler settings
# ============================================================
target_compile_features(CullingEngine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(CullingEngine PRIVATE /W3)
    # Disable specific MSVC warnings matching the original code's pragmas
    target_compile_options(CullingEngine PRIVATE /wd4996)
else()
    target_compile_options(CullingEngine PRIVATE -Wall -Wextra)
endif()

# macOS/iOS specific settings
if(APPLE)
    set_target_properties(CullingEngine PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
    if(CullingEngine_IOS)
        set_target_properties(CullingEngine PROPERTIES
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "12.0"
        )
    else()
        set_target_properties(CullingEngine PROPERTIES
            XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "10.15"
        )
    endif()
endif()

# Android specific settings
if(CullingEngine_ANDROID)
    # Require NEON for ARM
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armv7|armeabi)")
        target_compile_options(CullingEngine PRIVATE -mfpu=neon)
    endif()
endif()

# ============================================================
# Export macros for shared library
# ============================================================
if(CullingEngine_BUILD_SHARED)
    if(APPLE OR CullingEngine_ANDROID OR CullingEngine_LINUX)
        set_target_properties(CullingEngine PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
    endif()
endif()

# ============================================================
# Installation
# ============================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS CullingEngine
    EXPORT CullingEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT CullingEngineTargets
    FILE CullingEngineTargets.cmake
    NAMESPACE CullingEngine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CullingEngine
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CullingEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CullingEngineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CullingEngine
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CullingEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CullingEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CullingEngineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CullingEngine
)

# ============================================================
# Test project (independent)
# ============================================================
if(CullingEngine_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CullingEngine_BUILD_RENDER)
    add_subdirectory(tests/render)
endif()

message(STATUS "CullingEngine Platform: ${CullingEngine_PLATFORM}")
if(CullingEngine_BUILD_SHARED)
    set(CullingEngine_LIBRARY_TYPE "Shared")
else()
    set(CullingEngine_LIBRARY_TYPE "Static")
endif()
message(STATUS "CullingEngine Library Type: ${CullingEngine_LIBRARY_TYPE}")
