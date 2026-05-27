set(CullingEngine_DEPTH_SCENE_ASSETS
    assets/README.md
    assets/models/SOURCES.txt
    assets/models/cube.obj
    assets/models/octahedron.obj
    assets/models/pyramid.obj
    assets/models/spot.obj
    assets/models/suzanne.obj
    assets/models/teapot.obj
    assets/models/woody.obj
    assets/scenes/depth_scene.json
)
set_source_files_properties(${CullingEngine_DEPTH_SCENE_ASSETS}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

add_executable(CullingEngine_depth_scene_demo
    depth_scene_demo.cpp
    scene_assets.cpp
    scene_assets.h
    ${CullingEngine_DEPTH_SCENE_ASSETS}
)

target_link_libraries(CullingEngine_depth_scene_demo PRIVATE CullingEngine)
target_compile_features(CullingEngine_depth_scene_demo PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(CullingEngine_depth_scene_demo PRIVATE /W3 /wd4996)
    target_compile_definitions(CullingEngine_depth_scene_demo PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

get_target_property(CullingEngine_EXAMPLE_LIBRARY_TYPE CullingEngine TYPE)
if(WIN32 AND CullingEngine_EXAMPLE_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET CullingEngine_depth_scene_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:CullingEngine>
            $<TARGET_FILE_DIR:CullingEngine_depth_scene_demo>
    )
endif()

if(CullingEngine_BUILD_TESTS)
    add_test(NAME CullingEngine_depth_scene_demo
        COMMAND CullingEngine_depth_scene_demo
            --assets-dir ${CMAKE_CURRENT_SOURCE_DIR}/assets
            --output ${CMAKE_BINARY_DIR}/artifacts/depth_scene_demo/ctest-depth.png
    )
endif()
