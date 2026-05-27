cmake_minimum_required(VERSION 4.0)

project(GameServer C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ENGINE_ROOT always points to the shared src/ directory.
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

option(ENGINE_PROFILER_ENABLED "Enable perfetto profiler" ON)
option(ENGINE_MONGODB_ENABLED "Enable MongoDB driver support" ON)
option(ENGINE_PHYSICS_ENABLED "Enable Jolt Physics integration" ON)
option(HTTPS "Enable HTTPS/SSL support" OFF)
option(CMAKE_BENCHMARK_TESTING "Enable benchmark testing" OFF)

# ── Platform settings ────────────────────────────────────────────────
if (UNIX)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
    set (CXX_FLAGS
         -g
         -fPIC
         -Wall
         -D__STDC_FORMAT_MACROS
         -Wno-unused-parameter
         -Wno-unused-function
         -Wunused-variable
         -Wunused-value
         -Wshadow
         -Wcast-qual
         -Wcast-align
         -Wwrite-strings
         -Wsign-compare
         -Winvalid-pch
         -fms-extensions
         -Wfloat-equal
         -Wextra
    )
else (UNIX)
    set (CXX_FLAGS -DWIN32 -D_WIN32 -EHsc /utf-8 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif (UNIX)

string (REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if (UNIX)
    SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb -D_DEBUG")
    SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG")
endif (UNIX)

if (HTTPS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_CLIENT_SUPPORTS_SSL")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEVPP_HTTP_SERVER_SUPPORTS_SSL")
endif (HTTPS)

if (CMAKE_BENCHMARK_TESTING)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DH_BENCHMARK_TESTING=1")
endif (CMAKE_BENCHMARK_TESTING)

# ── MSVC compile options (from runtime) ────────────────────────────────
if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_compile_options(/bigobj /Zc:preprocessor /utf-8)
    add_compile_options(
        /wd4005 /wd4244 /wd4251 /wd4267 /wd4355 /wd4503 /wd4530
        /wd4577 /wd4715 /wd4800 /wd4819 /wd4996
    )
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── Platform libraries (from runtime) ─────────────────────────────────
if(UNIX)
    set(LIBRARIES event pthread)
else()
    set(LIBRARIES event_core_static event_extra_static)
endif()

# ── Output paths ──────────────────────────────────────────────────────
set (EXECUTABLE_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/bin)
set (LIBRARY_OUTPUT_PATH ${ENGINE_ROOT}/../artifacts/lib)
set (CMAKE_MACOSX_RPATH 0)

# ── Status ────────────────────────────────────────────────────────────
message (STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message (STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
message (STATUS "CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS})
message (STATUS "EXECUTABLE_OUTPUT_PATH " ${EXECUTABLE_OUTPUT_PATH})
message (STATUS "LIBRARY_OUTPUT_PATH " ${LIBRARY_OUTPUT_PATH})

# ── Runtime source-file organization ──────────────────────────────────
# Provides SERVERENGINE_SOURCES and per-module source lists.
include(${ENGINE_ROOT}/runtime/CMakeLists.txt)

# ═══════════════════════════════════════════════════════════════════════
# CloudEngine library (from runtime sources)
# ═══════════════════════════════════════════════════════════════════════

if(NOT TARGET CloudEngine)
    # ── libevent (built from source) ──────────────────────────────────
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

    # ── mongo-c-driver (static, bundled deps) ─────────────────────────
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
        if(TARGET public-header-warnings)
            set_property(TARGET public-header-warnings PROPERTY FOLDER "thirdparty")
        endif()
        if(TARGET mcd_rpc)
            set_property(TARGET mcd_rpc PROPERTY FOLDER "thirdparty")
        endif()
        if(TARGET mlib-ckdint-test)
            set_property(TARGET mlib-ckdint-test PROPERTY FOLDER "thirdparty")
        endif()
        if(TARGET mongo_c_driver_examples)
            set_property(TARGET mongo_c_driver_examples PROPERTY FOLDER "thirdparty")
        endif()
        if(TARGET mongo_c_driver_tests)
            set_property(TARGET mongo_c_driver_tests PROPERTY FOLDER "thirdparty")
        endif()
        if(TARGET mongoc-cxx-check)
            set_property(TARGET mongoc-cxx-check PROPERTY FOLDER "thirdparty")
        endif()
    endif()

    # ── Global include directories ───────────────────────────────────
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

    # ── CloudEngine library target ───────────────────────────────────
    add_library(CloudEngine SHARED ${SERVERENGINE_SOURCES})
    add_dependencies(CloudEngine copy_resources)
    set_target_properties(CloudEngine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

    target_include_directories(CloudEngine PUBLIC
        ${ENGINE_ROOT}
        ${ENGINE_ROOT}/runtime
        ${ENGINE_ROOT}/thirdparty/libevent/include
        ${CMAKE_BINARY_DIR}/libevent/include
        ${ENGINE_ROOT}/thirdparty/quill/quill/include
        ${ENGINE_ROOT}/thirdparty/lua/src
        ${ENGINE_ROOT}/thirdparty/glaze/glaze/include
        ${ENGINE_ROOT}/thirdparty/concurrentqueue/concurrentqueue
    )

    target_compile_definitions(CloudEngine PRIVATE ENGINE_BUILD)

    if(ENGINE_PHYSICS_ENABLED)
        target_compile_definitions(CloudEngine PUBLIC ENGINE_PHYSICS_ENABLED)
    endif()

    if(ENGINE_PROFILER_ENABLED)
        target_compile_definitions(CloudEngine PUBLIC ENGINE_PROFILER_ENABLED)
    endif()

    if(ENGINE_MONGODB_ENABLED)
        target_compile_definitions(CloudEngine PUBLIC ENGINE_MONGODB_ENABLED)
    endif()

    # ── JoltPhysics ───────────────────────────────────────────────────
    if(ENGINE_PHYSICS_ENABLED)
        add_subdirectory(${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics/Build jolt)
        set_target_properties(Jolt PROPERTIES FOLDER "thirdparty")
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)

        target_include_directories(CloudEngine PUBLIC
            ${ENGINE_ROOT}/thirdparty/JoltPhysics/JoltPhysics
        )
        target_link_libraries(CloudEngine PUBLIC Jolt)
    endif()

    # ── Link mongo-c-driver ──────────────────────────────────────────
    if(TARGET bson_static)
        target_link_libraries(CloudEngine PUBLIC bson_static)
    endif()
    if(TARGET mongoc_static)
        target_link_libraries(CloudEngine PUBLIC mongoc_static)
        if(WIN32)
            target_link_libraries(CloudEngine PUBLIC secur32 crypt32 Bcrypt ncrypt Dnsapi)
        endif()
    endif()

    # ── Link system libraries ────────────────────────────────────────
    target_link_directories(CloudEngine PUBLIC "${CMAKE_BINARY_DIR}/libevent/lib/$<CONFIG>")
    target_link_libraries(CloudEngine PUBLIC ${LIBRARIES})

    # ── Post-build: copy DLL to bin/ ──────────────────────────────────
    add_custom_command(TARGET CloudEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:CloudEngine>"
        "${ENGINE_ROOT}/../artifacts/bin/$<CONFIG>/$<TARGET_FILE_NAME:CloudEngine>"
        COMMENT "Copying CloudEngine.dll to bin/"
    )
endif()

# ═══════════════════════════════════════════════════════════════════════
# GameServer executable
# ═══════════════════════════════════════════════════════════════════════

# ── Lua 5.5.0 (source-form integration) ─────────────────────────────
# Core VM
set(GAMESERVER_LUA_CORE
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
# Standard libraries
set(GAMESERVER_LUA_LIB
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

add_executable(GameServer
    server.cc
    ${GAMESERVER_LUA_CORE}
    ${GAMESERVER_LUA_LIB}
)

add_dependencies(GameServer copy_resources)

set_target_properties(GameServer PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

target_include_directories(GameServer
    PRIVATE
        "${ENGINE_ROOT}"
        "${ENGINE_ROOT}/thirdparty/lua/src"
)

target_link_libraries(GameServer CloudEngine)

if(WIN32)
    target_link_libraries(GameServer ws2_32)
endif()
