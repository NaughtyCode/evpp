file(GLOB evnsq_SRCS CONFIGURE_DEPENDS *.cc *.h)
file(GLOB evnsq_PUBLIC_HEADERS CONFIGURE_DEPENDS *.h)

add_library(evnsq_static STATIC ${evnsq_SRCS})
target_link_libraries(evnsq_static ${LIBRARIES})
set_property(TARGET evnsq_static PROPERTY FOLDER "tests/apps")

if (UNIX)
    add_library(evnsq SHARED ${evnsq_SRCS})
    target_link_libraries(evnsq ${LIBRARIES})
    set_property(TARGET evnsq PROPERTY FOLDER "tests/apps")

    install (
      TARGETS evnsq
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib)
endif (UNIX)

install (
  TARGETS evnsq_static
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
install (FILES ${evnsq_PUBLIC_HEADERS} DESTINATION "include/evnsq")

if (NOT EVPP_VCPKG_BUILD)
    add_subdirectory(evnsqtail)
    add_subdirectory(test)
endif ()
