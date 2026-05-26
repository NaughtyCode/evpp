
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
file(GLOB evmc_SRCS CONFIGURE_DEPENDS *.cc
                    *.h
                    ${ENGINE_ROOT}/thirdparty/libhashkit/*.c)
file(GLOB evmc_PUBLIC_HEADERS CONFIGURE_DEPENDS *.h)

add_library(evmc_static STATIC ${evmc_SRCS})
target_link_libraries(evmc_static ${LIBRARIES})
set_property(TARGET evmc_static PROPERTY FOLDER "tests/apps")

if (UNIX)
    add_library(evmc SHARED ${evmc_SRCS})
    target_link_libraries(evmc ${LIBRARIES})
    set_property(TARGET evmc PROPERTY FOLDER "tests/apps")

    install (
      TARGETS evmc
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib)
endif (UNIX)

install (
  TARGETS evmc_static
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
install (FILES ${evmc_PUBLIC_HEADERS} DESTINATION "include/evmc")


if (NOT EVPP_VCPKG_BUILD)
    add_subdirectory(test)
endif ()