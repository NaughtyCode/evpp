cmake_minimum_required(VERSION 4.0)

project(engine_perfetto CXX)

add_library(engine_perfetto_static STATIC
    perfetto.cc
    perfetto.h
)
add_library(engine::perfetto ALIAS engine_perfetto_static)

target_include_directories(engine_perfetto_static PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(engine_perfetto_static PRIVATE /utf-8)
endif()

set_target_properties(engine_perfetto_static PROPERTIES
    OUTPUT_NAME perfetto
    FOLDER "thirdparty"
)
