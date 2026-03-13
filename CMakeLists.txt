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

# Mock Pico-8 executable
add_executable(mock_pico8 src/mock_pico8.cpp)
target_link_libraries(mock_pico8 PRIVATE Threads::Threads)

# LED Driver executable
add_executable(led_driver src/led_driver.cpp)
target_link_libraries(led_driver PRIVATE Threads::Threads rgbmatrix)
