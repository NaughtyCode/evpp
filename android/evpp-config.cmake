# evpp Android CMake configuration
# Include this in your Android project's CMakeLists.txt:
#
#   set(evpp_DIR "${CMAKE_CURRENT_SOURCE_DIR}/path/to/android/install/evpp/${ANDROID_ABI}")
#   find_package(evpp REQUIRED)
#   target_link_libraries(your_target evpp::evpp_static)

set(EVPP_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
set(EVPP_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib")

# Ensure include dir exists
if(NOT EXISTS "${EVPP_INCLUDE_DIR}/evpp/evpp.h")
    message(FATAL_ERROR "evpp headers not found at ${EVPP_INCLUDE_DIR}. Is the build complete?")
endif()

# Create imported library targets
add_library(evpp::evpp_static STATIC IMPORTED)
set_target_properties(evpp::evpp_static PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${EVPP_INCLUDE_DIR}"
    IMPORTED_LOCATION "${EVPP_LIB_DIR}/libevpp_static.a"
)

add_library(evpp::evpp_lite_static STATIC IMPORTED)
set_target_properties(evpp::evpp_lite_static PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${EVPP_INCLUDE_DIR}"
    IMPORTED_LOCATION "${EVPP_LIB_DIR}/libevpp_lite_static.a"
)

# evpp depends on: event, glog, and on Android: log
# The consumer must also link against these deps
set(EVPP_DEPENDENCIES event glog log android)
