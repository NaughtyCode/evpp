cmake_minimum_required(VERSION 4.0)

project(engine_kcp C)

add_library(engine_kcp_static STATIC
    ikcp.c
    ikcp.h
)
add_library(engine::kcp ALIAS engine_kcp_static)

target_include_directories(engine_kcp_static PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(engine_kcp_static PRIVATE /utf-8)
endif()

set_target_properties(engine_kcp_static PROPERTIES
    OUTPUT_NAME kcp
    FOLDER "thirdparty"
)
