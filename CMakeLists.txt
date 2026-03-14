cmake_minimum_required(VERSION 3.22)
project(hub75buf VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# We will need threads for a sleep loop in mock_pico8, and eventually perhaps for the led_matrix driver
find_package(Threads REQUIRED)

# Add rpi-rgb-led-matrix dependency using ExternalProject to use its Makefile
include(ExternalProject)
ExternalProject_Add(
    rgbmatrix_ext
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rpi-rgb-led-matrix
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make -C lib
    INSTALL_COMMAND ""
)

add_library(rgbmatrix STATIC IMPORTED)
set_target_properties(rgbmatrix PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rpi-rgb-led-matrix/lib/librgbmatrix.a
)
add_dependencies(rgbmatrix rgbmatrix_ext)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/rpi-rgb-led-matrix/include)

# Add wayland dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)

# Generate wlr-screencopy headers
add_custom_command(
    OUTPUT wlr-screencopy-unstable-v1-protocol.c wlr-screencopy-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code < ${CMAKE_CURRENT_SOURCE_DIR}/src/protocols/wlr-screencopy-unstable-v1.xml > wlr-screencopy-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header < ${CMAKE_CURRENT_SOURCE_DIR}/src/protocols/wlr-screencopy-unstable-v1.xml > wlr-screencopy-unstable-v1-client-protocol.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/protocols/wlr-screencopy-unstable-v1.xml
)

add_library(wl_capture STATIC src/wl_capture.cpp wlr-screencopy-unstable-v1-protocol.c)
target_include_directories(wl_capture PRIVATE ${WAYLAND_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wl_capture PRIVATE ${WAYLAND_LDFLAGS})

# Mock Pico-8 executable
add_executable(mock_pico8 src/mock_pico8.cpp)
target_link_libraries(mock_pico8 PRIVATE Threads::Threads)

# wl_hub75 executable
add_executable(wl_hub75 src/wl_hub75.cpp)
target_link_libraries(wl_hub75 PRIVATE Threads::Threads rgbmatrix wl_capture)

# Framebuffer Info utility
add_executable(fb_info src/fb_info.c)
