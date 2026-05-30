cmake_minimum_required(VERSION 4.0)

project(engine_lua C)

include(${CMAKE_CURRENT_LIST_DIR}/lua_sources.cmake)

add_library(engine_lua_static STATIC ${ENGINE_LUA_SOURCES})
add_library(engine::lua ALIAS engine_lua_static)

target_include_directories(engine_lua_static
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src
)

if(MSVC)
    target_compile_options(engine_lua_static PRIVATE /utf-8)
endif()

set_target_properties(engine_lua_static PROPERTIES
    FOLDER "thirdparty"
    OUTPUT_NAME "lua"
)
