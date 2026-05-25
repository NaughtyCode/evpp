if(ENGINE_PHYSICS_ENABLED)
    add_executable(physics_smoke physics_smoke.cc)
    set_target_properties(physics_smoke PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        FOLDER "tests")
    target_link_libraries(physics_smoke CloudEngine)
endif()
